// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

using Microsoft.CodeAnalysis;
using Microsoft.CodeAnalysis.CSharp.Syntax;

namespace VeroScript.CSharpTranspiler;

public static partial class CSharpTranspiler
{
	private sealed partial class Emitter
	{
		private bool TryStringConstant(ExpressionSyntax expression, out string text)
		{
			text = "";
			if (model.GetTypeInfo(expression).Type?.SpecialType != SpecialType.System_String) return false;
			if (expression.DescendantNodesAndSelf().Any(node => node is CheckedExpressionSyntax)) return false;
			Optional<object?> constant = model.GetConstantValue(expression);
			if (!constant.HasValue || constant.Value is not string value) return false;
			text = HasUnpairedSurrogate(value)
				? FailLiteral(expression, "string constant containing an unpaired UTF-16 surrogate") : Quote(value);
			return true;
		}

		private string BooleanText(string expression)
		{
			string helper = NumericHelper("bool_text",
				"string bool_text(bool value) { return value ? \"True\" : \"False\"; }");
			return helper + "(" + expression + ")";
		}

		private string StringOperand(ExpressionSyntax expression)
		{
			SpecialType type = model.GetTypeInfo(expression).Type?.SpecialType ?? SpecialType.None;
			if (type == SpecialType.System_String) return Expression(expression);
			if (type == SpecialType.System_Boolean) return BooleanText(Expression(expression));
			if (IsIntegral(type)) return "std::to_string(" + Expression(expression) + ")";
			return FailLiteral(expression, "string concatenation with a non-string/non-integer/non-boolean operand");
		}

		private string ConsoleInvocation(InvocationExpressionSyntax invocation, IMethodSymbol method)
		{
			if (invocation.ArgumentList.Arguments.Count == 0 && method.Name == "WriteLine") return "std::print(\"\")";
			if (invocation.ArgumentList.Arguments.Count != 1)
			{
				return FailLiteral(invocation, "Console formatted or multi-argument overload");
			}
			ArgumentSyntax argument = invocation.ArgumentList.Arguments[0];
			SpecialType type = model.GetTypeInfo(argument.Expression).Type?.SpecialType ?? SpecialType.None;
			if (type != SpecialType.System_String && type != SpecialType.System_Boolean && !IsIntegral(type))
			{
				return FailLiteral(argument, "Console output of floating-point, enum, object or array values");
			}
			string value = Argument(argument);
			if (type == SpecialType.System_Boolean)
			{
				value = BooleanText(value);
			}
			return (method.Name == "Write" ? "std::console::write(" : "std::print(") + value + ")";
		}

		private string StringCompoundAssignment(AssignmentExpressionSyntax assignment, string left)
		{
			string right = StringOperand(assignment.Right);
			string helper = NumericHelper("assign_add_string",
				"string assign_add_string(ref string value, fn()->string rhs) { string previous = value; "
				+ "string right = rhs(); value = previous + right; return value; }");
			return helper + "(ref " + left + ", [&]() -> string => " + right + ")";
		}
	}
}

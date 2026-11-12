// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptDebugEval.h"

#include "VeroScriptCompiler.h"
#include "VeroScriptLexer.h"
#include "VeroScriptMethodKey.h"
#include "VeroScriptParser.h"
#include "VeroScriptVm.h"

#include <algorithm>
#include <iostream>
#include <sstream>

namespace VeroScript::DebugEval
{
	namespace
	{
		std::unique_ptr<FBytecodeModule> CompileSource(
			const std::string& Source,
			std::string& OutError, const std::string_view FileLabel)
		{
			OutError.clear();
			VeroScriptLexer Lexer(Source, FileLabel);
			const std::vector<FToken>& Tokens = Lexer.Tokenize();
			if (Lexer.HasError())
			{
				OutError = "lexer error";
				return nullptr;
			}
			VeroScriptParser Parser(Tokens, FileLabel);
			std::unique_ptr<FProgram> Program = Parser.ParseProgram();
			if (!Program || Parser.HasError())
			{
				OutError = Parser.GetError().Message;
				return nullptr;
			}
			VeroScriptCompiler Compiler;
			std::unique_ptr<FBytecodeModule> Module = Compiler.Compile(*Program);
			if (!Module)
			{
				OutError = Compiler.GetError().Message;
			}
			return Module;
		}

		bool LooksBoolExpr(const std::string_view Expression)
		{
			return Expression.find("==") != std::string_view::npos
				|| Expression.find("!=") != std::string_view::npos
				|| Expression.find("<=") != std::string_view::npos
				|| Expression.find(">=") != std::string_view::npos
				|| Expression.find("&&") != std::string_view::npos
				|| Expression.find("||") != std::string_view::npos
				|| Expression.find('<') != std::string_view::npos
				|| Expression.find('>') != std::string_view::npos
				|| Expression.find('!') != std::string_view::npos;
		}

		std::string BuildEvalSource(
			const std::string_view ReturnType,
			const std::vector<std::string>& ParamNames,
			const std::vector<std::string>& ParamTypes,
			const std::string_view Expression,
			const std::string_view DeclarationPrelude)
		{
			std::ostringstream Stream;
			if (!DeclarationPrelude.empty())
			{
				Stream << DeclarationPrelude << '\n';
			}
			Stream << ReturnType << " __vs_dbg_eval(";
			for (std::size_t Index = 0; Index < ParamNames.size(); ++Index)
			{
				if (Index > 0)
				{
					Stream << ", ";
				}
				Stream << ParamTypes[Index] << ' ' << ParamNames[Index];
			}
			Stream << ") {\n\treturn (" << Expression << ");\n}\n";
			Stream << "void main() {}\n";
			return Stream.str();
		}

		bool TryRunEvalModule(
			const FBytecodeModule& Module,
			const std::vector<FValue>& ArgumentValues,
			FValue& OutValue,
			std::string& OutError, const std::shared_ptr<const IVmBackendFactory>& VmFactory)
		{
			std::int32_t FunctionIndex = -1;
			for (std::size_t Index = 0; Index < Module.Functions.size(); ++Index)
			{
				const FBytecodeFunction& Function = Module.Functions[Index];
				const std::string Leaf = Function.Name.substr(
					FindLinkageIdentityStart(Function.Name));
				if (Leaf == "__vs_dbg_eval" || Leaf.rfind("__vs_dbg_eval#", 0) == 0)
				{
					FunctionIndex = static_cast<std::int32_t>(Index);
					break;
				}
			}
			if (FunctionIndex < 0)
			{
				for (std::size_t Index = 0; Index < Module.Functions.size(); ++Index)
				{
					if (Module.Functions[Index].Name.find("__vs_dbg_eval") != std::string::npos)
					{
						FunctionIndex = static_cast<std::int32_t>(Index);
						break;
					}
				}
			}
			if (FunctionIndex < 0)
			{
				OutError = "debug eval function missing";
				return false;
			}

			std::ostringstream DiscardOut;
			std::ostringstream Errors;
			VeroScriptVm Vm(DiscardOut, std::cin, Errors);
			if (!Vm.SetVmBackendFactory(VmFactory, OutError)) return false;
			FValue Result;
			if (!Vm.ExecuteFunction(
					Module,
					static_cast<std::uint16_t>(FunctionIndex),
					ArgumentValues,
					nullptr,
						&Result))
			{
				OutError = Vm.HasError() ? Vm.GetError().Message : Errors.str();
				if (OutError.empty())
				{
					OutError = "debug eval failed";
				}
				return false;
			}
			OutValue = std::move(Result);
			return true;
		}
	}

	std::string TypeNameOfValue(const FValue& Value)
	{
		if (std::holds_alternative<FScriptVariantHandle>(Value)) return "variant";
		if (std::holds_alternative<std::int8_t>(Value))
		{
			return "char";
		}
		if (std::holds_alternative<std::uint8_t>(Value))
		{
			return "unsigned char";
		}
		if (std::holds_alternative<std::int16_t>(Value))
		{
			return "short";
		}
		if (std::holds_alternative<std::uint16_t>(Value))
		{
			return "unsigned short";
		}
		if (std::holds_alternative<std::int32_t>(Value))
		{
			return "int";
		}
		if (std::holds_alternative<std::int64_t>(Value))
		{
			return "int64";
		}
		if (std::holds_alternative<std::uint32_t>(Value))
		{
			return "unsigned";
		}
		if (std::holds_alternative<std::uint64_t>(Value))
		{
			return "uint64";
		}
		if (std::holds_alternative<double>(Value))
		{
			return "double";
		}
		if (std::holds_alternative<float>(Value))
		{
			return "float";
		}
		if (std::holds_alternative<bool>(Value))
		{
			return "bool";
		}
		if (std::holds_alternative<std::string>(Value))
		{
			return "string";
		}
		if (std::holds_alternative<std::u16string>(Value))
		{
			return "wstring";
		}
		if (const auto* Array = std::get_if<FDynamicArrayHandle>(&Value))
		{
			return *Array && !(*Array)->TypeName.empty() ? (*Array)->TypeName : "array<int>";
		}
		if (const auto* Arr = std::get_if<FNativeArray>(&Value))
		{
			return Arr->TypeName().empty() ? "int[]" : Arr->TypeName();
		}
		if (const auto* Map = std::get_if<FMapHandle>(&Value))
		{
			return *Map && !(*Map)->TypeName.empty() ? (*Map)->TypeName : "map<int,int>";
		}
		if (const auto* Set = std::get_if<FSetHandle>(&Value))
		{
			return *Set && !(*Set)->TypeName.empty() ? (*Set)->TypeName : "set<int>";
		}
		if (const auto* Queue = std::get_if<FQueueHandle>(&Value))
		{
			return *Queue && !(*Queue)->TypeName.empty() ? (*Queue)->TypeName : "queue<int>";
		}
		if (const auto* Stack = std::get_if<FStackHandle>(&Value))
		{
			return *Stack && !(*Stack)->TypeName.empty() ? (*Stack)->TypeName : "stack<int>";
		}
		if (const auto* Ordered = std::get_if<FOrderedMapHandle>(&Value))
		{
			return *Ordered && !(*Ordered)->TypeName.empty()
				? (*Ordered)->TypeName
				: "ordered_map<int,int>";
		}
		if (const auto* Deque = std::get_if<FDequeHandle>(&Value))
		{
			return *Deque && !(*Deque)->TypeName.empty() ? (*Deque)->TypeName : "deque<int>";
		}
		if (const auto* Object = std::get_if<FObjectHandle>(&Value))
		{
			if (*Object && !(*Object)->GetTypeName().empty())
			{
				return (*Object)->GetTypeName();
			}
			return "object";
		}
		if (const auto* Function = std::get_if<FFunctionHandle>(&Value))
		{
			return *Function && !(*Function)->TypeName.empty() ? (*Function)->TypeName : "fn";
		}
		if (const auto* Channel = std::get_if<FChannelHandle>(&Value))
		{
			if (!*Channel)
			{
				return "channel";
			}
			std::string TypeName;
			std::string ElementTypeName;
			(*Channel)->GetTypeNames(TypeName, ElementTypeName);
			return TypeName.empty() ? "channel" : TypeName;
		}
		if (const auto* Host = std::get_if<FHostObjectHandle>(&Value))
		{
			return *Host && !(*Host)->TypeName.empty() ? (*Host)->TypeName : "object";
		}
		if (const auto* Enum = std::get_if<FEnumValue>(&Value))
		{
			return Enum->TypeName.empty() ? "int" : Enum->TypeName;
		}
		return "int";
	}

	FEvalResult EvaluateExpression(
		const std::string_view Expression,
		const std::vector<std::string>& ParamNames,
		const std::vector<std::string>& ParamTypes,
		const std::vector<FValue>& ArgumentValues,
		const std::string_view ExpectedReturnType,
		const std::string_view DeclarationPrelude, const std::string_view FileLabel,
		std::shared_ptr<const IVmBackendFactory> VmFactory)
	{
		FEvalResult Result;
		if (Expression.empty())
		{
			Result.Error = "empty expression";
			return Result;
		}
		if (ParamNames.size() != ParamTypes.size()
			|| ParamNames.size() != ArgumentValues.size())
		{
			Result.Error = "debug eval argument mismatch";
			return Result;
		}

		std::vector<std::string> ReturnCandidates;
		const auto AppendCandidate =
			[&ReturnCandidates](const std::string_view TypeName)
			{
				if (!TypeName.empty()
						&& std::find(
							ReturnCandidates.begin(),
							ReturnCandidates.end(),
							TypeName) == ReturnCandidates.end())
				{
					ReturnCandidates.emplace_back(TypeName);
				}
			};
		if (!ExpectedReturnType.empty())
		{
			AppendCandidate(ExpectedReturnType);
		}
		else
		{
			std::string_view Trimmed = Expression;
			while (!Trimmed.empty() && (Trimmed.front() == ' ' || Trimmed.front() == '\t'))
			{
				Trimmed.remove_prefix(1);
			}
			while (!Trimmed.empty() && (Trimmed.back() == ' ' || Trimmed.back() == '\t'))
			{
				Trimmed.remove_suffix(1);
			}
			for (std::size_t Index = 0; Index < ParamNames.size(); ++Index)
			{
				if (Trimmed == ParamNames[Index])
				{
					AppendCandidate(ParamTypes[Index]);
					break;
				}
			}
			if (LooksBoolExpr(Expression))
			{
				AppendCandidate("bool");
				AppendCandidate("int");
			}
			else
			{
				AppendCandidate("int");
				AppendCandidate("bool");
				AppendCandidate("double");
				AppendCandidate("string");
				AppendCandidate("wstring");
				AppendCandidate("unsigned");
			}
			// Container/host-object expressions cannot be represented by the scalar
			// fallback list. Local types provide safe additional candidates for watches.
			for (const std::string& ParamType : ParamTypes)
			{
				AppendCandidate(ParamType);
			}
		}

		std::string LastError;
		for (const std::string& ReturnType : ReturnCandidates)
		{
			const std::string Source =
				BuildEvalSource(
					ReturnType,
					ParamNames,
					ParamTypes,
					Expression,
					DeclarationPrelude);
			std::string CompileError;
			std::unique_ptr<FBytecodeModule> Module = CompileSource(Source, CompileError, FileLabel);
			if (!Module)
			{
				LastError = "failed to compile debug expression as " + ReturnType;
				if (!CompileError.empty())
				{
					LastError += ": " + CompileError;
				}
				continue;
			}
			FValue Value;
			std::string RunError;
			if (!TryRunEvalModule(*Module, ArgumentValues, Value, RunError, VmFactory))
			{
				LastError = std::move(RunError);
				continue;
			}
			Result.bOk = true;
			Result.Value = std::move(Value);
			return Result;
		}
		Result.Error = LastError.empty() ? "failed to evaluate expression" : LastError;
		return Result;
	}
}

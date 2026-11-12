// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptAst.h"
#include "VeroScriptLexer.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace VeroScript::ParserDetail
{
	bool IsTypeKeyword(std::string_view Lexeme);
	bool IsUnsignedBaseTypeKeyword(std::string_view Lexeme);
	bool IsContainerTypeKeyword(std::string_view Lexeme);
	bool HasMethodWithSignature(
		const std::vector<FMethodDecl>& Methods,
		const FMethodDecl& Candidate);
	bool HasFunctionWithSignature(
		const std::vector<FFunctionDecl>& Functions,
		const FFunctionDecl& Candidate);
	bool HasConstructorWithSignature(
		const std::vector<FConstructorDecl>& Constructors,
		const FConstructorDecl& Candidate);
	bool HasValidVariadicPlacement(const std::vector<FFunctionParam>& Parameters);
	bool HasValidTrailingDefaults(const std::vector<FFunctionParam>& Parameters);
	void PreserveDeclaredHostSignature(
		FDeclMeta& DeclMeta,
		std::string_view ReturnType,
		const std::vector<FFunctionParam>& Parameters,
		const std::unordered_set<std::string>& HostTypeNames);
	std::string OperatorNameForValidation(const std::string& Name);
	bool ValidateMemberOperatorSignature(
		const std::string& Name,
		const std::string& ReturnType,
		const bool bIsStatic,
		const std::vector<FFunctionParam>& Parameters,
		const std::string& LastParameterType,
		std::string& OutError);
	bool ValidateFreeOperatorSignature(
		const std::string& Name,
		const std::string& ReturnType,
		const std::vector<FFunctionParam>& Parameters,
		const std::string& LastParameterType,
		std::string& OutError);
	bool TypeMentionsParam(const std::string& TypeName, const std::string& Param);
	bool IsNameToken(const FToken& Token);
	// Returns Tokens.size() when the declaration head has no qualified name.
	std::size_t FindQualifiedDefinitionSeparator(const std::vector<FToken>& Tokens, std::size_t Begin);
	bool IsDeclVisible(
		std::string_view DeclModulePath,
		bool bExported,
		std::string_view CurrentModulePath,
		const std::unordered_set<std::string>* VisibleModulePaths);
	bool ParseIntegerLiteral(
		std::string_view Lexeme,
		std::int32_t& OutSignedValue,
		std::uint32_t& OutUnsignedValue,
		bool& bOutUnsigned,
		std::string& OutError,
		bool* bOutIs64 = nullptr,
		std::int64_t* OutSignedValue64 = nullptr,
		std::uint64_t* OutUnsignedValue64 = nullptr,
		bool bAllowSignedMinMagnitude = false,
		bool* bOutParsedSignedMin = nullptr);
	bool ParseFloatLiteral(
		std::string_view Lexeme,
		double& OutValue,
		std::string& OutError);
	bool DecodeStringLiteral(
		std::string_view Lexeme,
		std::string& OutValue,
		std::string& OutError,
		std::uint32_t* OutErrorOffset = nullptr);
	bool DecodeModuleImportPath(std::string_view Lexeme, std::string &OutValue,
								std::string &OutError, std::uint32_t *OutErrorOffset = nullptr);
	bool DecodeWStringLiteral(
		std::string_view Lexeme,
		std::u16string& OutValue,
		std::string& OutError,
		std::uint32_t* OutErrorOffset = nullptr);
	bool DecodeCharacterLiteral(
		std::string_view Lexeme,
		std::int8_t& OutValue,
		std::string& OutError,
		std::uint32_t* OutErrorOffset = nullptr);
	bool IsValidAssignmentTarget(const FExpr& Expression);
	EBinaryOp CompoundAssignOp(std::string_view Symbol);
}

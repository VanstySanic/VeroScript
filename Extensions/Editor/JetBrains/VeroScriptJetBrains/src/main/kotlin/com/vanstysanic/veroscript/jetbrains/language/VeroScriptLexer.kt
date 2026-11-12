// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

package com.vanstysanic.veroscript.jetbrains.language

import com.intellij.lexer.LexerBase
import com.intellij.psi.TokenType
import com.intellij.psi.tree.IElementType

object VeroScriptTokens {
	val identifier = IElementType("IDENTIFIER", VeroScriptLanguage)
	val keyword = IElementType("KEYWORD", VeroScriptLanguage)
	val number = IElementType("NUMBER", VeroScriptLanguage)
	val string = IElementType("STRING", VeroScriptLanguage)
	val lineComment = IElementType("LINE_COMMENT", VeroScriptLanguage)
	val blockComment = IElementType("BLOCK_COMMENT", VeroScriptLanguage)
	val preprocessor = IElementType("PREPROCESSOR", VeroScriptLanguage)
	val operator = IElementType("OPERATOR", VeroScriptLanguage)
	val leftParen = IElementType("(", VeroScriptLanguage)
	val rightParen = IElementType(")", VeroScriptLanguage)
	val leftBrace = IElementType("{", VeroScriptLanguage)
	val rightBrace = IElementType("}", VeroScriptLanguage)
	val leftBracket = IElementType("[", VeroScriptLanguage)
	val rightBracket = IElementType("]", VeroScriptLanguage)
}

/** The editor lexer deliberately leaves semantic classification to the language server. */
class VeroScriptLexer : LexerBase() {
	private var buffer: CharSequence = ""
	private var end = 0
	private var start = 0
	private var position = 0
	private var type: IElementType? = null

	override fun start(buffer: CharSequence, startOffset: Int, endOffset: Int, initialState: Int) {
		this.buffer = buffer
		end = endOffset
		position = startOffset
		advance()
	}

	override fun getState() = 0
	override fun getTokenType() = type
	override fun getTokenStart() = start
	override fun getTokenEnd() = position
	override fun getBufferSequence() = buffer
	override fun getBufferEnd() = end

	override fun advance() {
		start = position
		if (position >= end) {
			type = null
			return
		}
		val first = buffer[position++]
		type = when {
			first.isWhitespace() -> {
				while (position < end && buffer[position].isWhitespace()) position++
				TokenType.WHITE_SPACE
			}
			first == '/' && peek('/') -> {
				position++
				while (position < end) {
					if (buffer[position] == '\n' || buffer[position] == '\r') {
						// Preprocessing splices backslash-LF/CRLF before interpreting line comments.
						val crlf = buffer[position] == '\r' && position + 1 < end && buffer[position + 1] == '\n'
						if (buffer[position - 1] != '\\' || buffer[position] == '\r' && !crlf) break
						position += if (crlf) 2
						else 1
					}
					else position++
				}
				VeroScriptTokens.lineComment
			}
			first == '/' && peek('*') -> {
				position++
				while (position < end) {
					if (buffer[position++] == '*' && peek('/')) {
						position++
						break
					}
				}
				VeroScriptTokens.blockComment
			}
			first in "rLl" && peek('"') -> {
				position++
				val raw = first == 'r'
				while (position < end) {
					val next = buffer[position++]
					if (next == '"') break
					if (!raw && next == '\\' && position < end) position++
					else if (!raw && (next == '\n' || next == '\r')) break
				}
				VeroScriptTokens.string
			}
			first == '"' || first == '\'' -> {
				while (position < end) {
					val next = buffer[position++]
					if (next == '\\' && position < end) position++
					else if (next == first || next == '\n' || next == '\r') break
				}
				VeroScriptTokens.string
			}
			first == '#' -> {
				while (position < end && (buffer[position].isLetter() || buffer[position] == '_')) position++
				VeroScriptTokens.preprocessor
			}
			first.isDigit() || first == '.' && position < end && buffer[position].isDigit() -> {
				val matcher = numericLiteral.matcher(buffer).region(start, end)
				if (matcher.lookingAt()) position = matcher.end()
				VeroScriptTokens.number
			}
			first.isLetter() || first == '_' -> {
				while (position < end && (buffer[position].isLetterOrDigit() || buffer[position] == '_')) position++
				if (buffer.subSequence(start, position).toString() in keywords) VeroScriptTokens.keyword
				else VeroScriptTokens.identifier
			}
			else -> when (first) {
				'(' -> VeroScriptTokens.leftParen
				')' -> VeroScriptTokens.rightParen
				'{' -> VeroScriptTokens.leftBrace
				'}' -> VeroScriptTokens.rightBrace
				'[' -> VeroScriptTokens.leftBracket
				']' -> VeroScriptTokens.rightBracket
				else -> VeroScriptTokens.operator
			}
		}
	}

	private fun peek(character: Char) = position < end && buffer[position] == character

	companion object {
		private const val integerSuffix = "(?:[iI]64|[uU]64|[uU][lL]?|[lL][uU]?)?"
		private val numericLiteral = java.util.regex.Pattern.compile(
			"(?:0[xX][0-9a-fA-F_]+$integerSuffix|0[bB][01_]+$integerSuffix|" +
				"(?:[0-9][0-9_]*(?:\\.[0-9_]*)?|\\.[0-9_]+)(?:[eE][+-]?[0-9_]+)?(?:[fF]|$integerSuffix))"
		)

		internal val keywords = setOf(
			"void", "bool", "char", "short", "int", "long", "int8", "uint8", "int16", "uint16",
			"int32", "uint32", "int64", "uint64", "float", "double", "string", "wstring", "unsigned",
			"auto", "channel", "array", "map", "multimap", "set", "multiset", "queue", "priority_queue",
			"stack", "ordered_map", "ordered_multimap", "deque", "ordered_multiset", "fn", "function",
			"if", "else", "while", "do", "for", "switch", "case", "default", "return", "break", "continue",
			"fallthrough", "goto", "try", "catch", "finally", "throw", "struct", "class", "interface", "enum",
			"import", "export", "using", "typedef", "namespace", "friend", "new", "delete", "this", "const", "constexpr",
			"ref", "static", "explicit", "public", "protected", "private", "virtual", "override", "final",
			"where", "operator", "template", "in", "true", "false", "nullptr"
		)
	}
}

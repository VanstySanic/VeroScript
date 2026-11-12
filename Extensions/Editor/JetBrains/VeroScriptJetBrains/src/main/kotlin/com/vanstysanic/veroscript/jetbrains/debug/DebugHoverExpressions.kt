// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

package com.vanstysanic.veroscript.jetbrains.debug

import com.intellij.openapi.util.TextRange
import com.intellij.psi.TokenType
import com.intellij.xdebugger.evaluation.ExpressionInfo
import com.vanstysanic.veroscript.jetbrains.language.VeroScriptLexer
import com.vanstysanic.veroscript.jetbrains.language.VeroScriptTokens

/** Extracts read-only name paths; hover never adds a call or evaluates a whole statement. */
internal object DebugHoverExpressions {
	private data class Token(val start: Int, val end: Int, val text: String, val name: Boolean)

	fun at(text: CharSequence, offset: Int): ExpressionInfo? {
		if (offset !in 0 until text.length) return null
		val lexer = VeroScriptLexer()
		lexer.start(text)
		val path = mutableListOf<Token>()
		while (lexer.tokenType != null && lexer.tokenStart <= offset) {
			val spelling = if (lexer.tokenType == VeroScriptTokens.identifier || lexer.tokenType == VeroScriptTokens.keyword ||
				lexer.tokenType == VeroScriptTokens.operator) text.subSequence(lexer.tokenStart, lexer.tokenEnd).toString()
				else ""
			val name = lexer.tokenType == VeroScriptTokens.identifier ||
				lexer.tokenType == VeroScriptTokens.keyword && spelling == "this"
			if (name || lexer.tokenType == VeroScriptTokens.operator && (spelling == "." || spelling == ":")) {
				path.add(Token(lexer.tokenStart, lexer.tokenEnd, spelling, name))
			}
			else if (lexer.tokenType != TokenType.WHITE_SPACE && lexer.tokenType != VeroScriptTokens.lineComment &&
				lexer.tokenType != VeroScriptTokens.blockComment) path.clear()
			if (offset < lexer.tokenEnd) {
				if (!name) return null
				val end = lexer.tokenEnd
				lexer.advance()
				while (lexer.tokenType == TokenType.WHITE_SPACE || lexer.tokenType == VeroScriptTokens.lineComment ||
					lexer.tokenType == VeroScriptTokens.blockComment) lexer.advance()
				if (lexer.tokenType == VeroScriptTokens.leftParen) return null
				var first = path.lastIndex
				while (first > 0) {
					val separator = when {
						path[first - 1].text == "." -> 1
						first >= 2 && path[first - 1].text == ":" && path[first - 2].text == ":" &&
							path[first - 2].end == path[first - 1].start -> 2
						else -> 0
					}
					if (separator == 0) break
					val base = first - separator - 1
					// Calls, indexed bases and incomplete paths need an explicit evaluation.
					if (base < 0 || !path[base].name) return null
					first = base
				}
				return ExpressionInfo(TextRange(path[first].start, end))
			}
			lexer.advance()
		}
		return null
	}
}

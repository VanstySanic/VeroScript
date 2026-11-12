// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

package com.vanstysanic.veroscript.jetbrains.language

import com.intellij.lexer.DelegateLexer
import com.intellij.psi.TokenType
import com.intellij.psi.tree.IElementType

internal object VeroScriptDocumentationTokens {
	val qualifier = IElementType("DOC_QUALIFIER", VeroScriptLanguage)
	val function = IElementType("DOC_FUNCTION", VeroScriptLanguage)
	val type = IElementType("DOC_TYPE", VeroScriptLanguage)
}

/** Local signature presentation only; source files keep the ordinary lexer and LSP classification. */
internal class VeroScriptDocumentationLexer : DelegateLexer(VeroScriptLexer()) {
	private data class Token(val start: Int, val type: IElementType, val text: String)
	private val roles = mutableMapOf<Int, IElementType>()

	override fun start(buffer: CharSequence, startOffset: Int, endOffset: Int, initialState: Int) {
		roles.clear()
		val lexer = VeroScriptLexer()
		lexer.start(buffer, 0, endOffset, 0)
		val tokens = mutableListOf<Token>()
		while (lexer.tokenType != null) {
			val type = lexer.tokenType!!
			if (type != TokenType.WHITE_SPACE && type != VeroScriptTokens.lineComment && type != VeroScriptTokens.blockComment) {
				tokens += Token(lexer.tokenStart, type, buffer.subSequence(lexer.tokenStart, lexer.tokenEnd).toString())
			}
			lexer.advance()
		}
		fun text(index: Int) = tokens.getOrNull(index)?.text
		fun qualified(index: Int) = text(index + 1) == ":" && text(index + 2) == ":"
		tokens.forEachIndexed { index, token ->
			if (token.type == VeroScriptTokens.identifier) {
				val role = when {
					qualified(index) -> VeroScriptDocumentationTokens.qualifier
					text(index - 1) in typeDeclarations -> VeroScriptDocumentationTokens.type
					text(index + 1) == "(" -> VeroScriptDocumentationTokens.function
					tokens.getOrNull(index + 1)?.type == VeroScriptTokens.identifier -> VeroScriptDocumentationTokens.type
					else -> null
				}
				if (role != null) roles[token.start] = role
			}
		}

		// Match generic delimiters once, then mark confirmed type-argument intervals in linear time.
		// A bare comparison such as `left < right > limit` must not turn into a generic type.
		val stack = ArrayDeque<Int>()
		val closes = IntArray(tokens.size) { -1 }
		tokens.forEachIndexed { index, token ->
			when (token.text) {
				"<" -> stack.addLast(index)
				">" -> if (stack.isNotEmpty()) closes[stack.removeLast()] = index
				";", "{", "}", "=" -> stack.clear()
			}
		}
		val coverage = IntArray(tokens.size + 1)
		var depth = 0
		tokens.forEachIndexed { index, token ->
			depth += coverage[index]
			val previous = tokens.getOrNull(index - 1)
			val declarationName = text(index + 1) in parameterEnds && previous != null &&
				(roles[previous.start] == VeroScriptDocumentationTokens.type ||
					previous.type == VeroScriptTokens.keyword && previous.text !in typeModifiers ||
					previous.text in typeSuffixes)
			if (depth > 0 && token.type == VeroScriptTokens.identifier && !qualified(index) && !declarationName) {
				roles[token.start] = VeroScriptDocumentationTokens.type
			}
			val close = closes[index]
			val owner = tokens.getOrNull(index - 1)
			if (close >= 0 && owner != null && (owner.text in genericTypes ||
					roles[owner.start] == VeroScriptDocumentationTokens.type)) {
				if (owner.text in softGenericTypes) roles[owner.start] = VeroScriptDocumentationTokens.type
				coverage[index + 1]++
				coverage[close]--
			}
		}
		super.start(buffer, startOffset, endOffset, initialState)
	}

	override fun getTokenType(): IElementType? = roles[tokenStart] ?: super.getTokenType()

	companion object {
		private val parameterEnds = setOf(",", ")", "=")
		private val typeModifiers = setOf("const", "ref", "unsigned")
		private val typeSuffixes = setOf(">", "]", "&")
		private val typeDeclarations = setOf("class", "struct", "interface", "enum", "typedef", "new")
		private val softGenericTypes = setOf("handle", "shared", "weak")
		private val genericTypes = setOf("array", "map", "multimap", "set", "multiset", "queue", "priority_queue",
			"stack", "ordered_map", "ordered_multimap", "deque", "ordered_multiset", "channel", "fn") + softGenericTypes
	}
}

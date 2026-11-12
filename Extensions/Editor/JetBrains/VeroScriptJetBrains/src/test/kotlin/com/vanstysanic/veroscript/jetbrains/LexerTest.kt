// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

package com.vanstysanic.veroscript.jetbrains

import com.intellij.psi.tree.IElementType
import com.vanstysanic.veroscript.jetbrains.language.VeroScriptLexer
import com.vanstysanic.veroscript.jetbrains.language.VeroScriptTokens
import com.vanstysanic.veroscript.jetbrains.language.VeroScriptDocumentationLexer
import com.vanstysanic.veroscript.jetbrains.language.VeroScriptDocumentationTokens
import org.junit.Assert.*
import org.junit.Test

class LexerTest {
	@Test fun continuedLineCommentsRetainRestartBoundariesAndStopBeforeOrdinaryCode() {
		for (eol in listOf("\n", "\r\n")) {
			val comment = "// continued \\" + eol + "void ignored() {} \\" + eol + "#define IGNORED 1"
			val text = comment + eol + "int active;"
			val tokens = scan(text)
			assertEquals(VeroScriptTokens.lineComment, tokens.first().type)
			assertEquals(comment, text.substring(tokens.first().start, tokens.first().end))
			assertTrue(tokens.any { it.type == VeroScriptTokens.identifier && text.substring(it.start, it.end) == "active" })
			tokens.forEachIndexed { index, token -> assertEquals(tokens.drop(index), scan(text, token.start, token.state)) }
			val ordinary = "// trailing space \\ " + eol + "int active;"
			assertTrue(scan(ordinary).any { it.type == VeroScriptTokens.keyword && ordinary.substring(it.start, it.end) == "int" })
			for (limit in 0..text.length) {
				val bounded = object : CharSequence {
					override val length = text.length
					override fun get(index: Int): Char {
						assertTrue("Read past a truncated comment", index < limit)
						return text[index]
					}
					override fun subSequence(startIndex: Int, endIndex: Int): CharSequence {
						assertTrue(endIndex <= limit)
						return text.subSequence(startIndex, endIndex)
					}
				}
				val lexer = VeroScriptLexer()
				lexer.start(bounded, 0, limit, 0)
				while (lexer.tokenType != null) {
					assertTrue(lexer.tokenEnd <= limit)
					lexer.advance()
				}
			}
		}
		var reads = 0
		val text = "// continued \\\n" + "still commented \\\n".repeat(4096) + "end"
		val counted = object : CharSequence {
			override val length = text.length
			override fun get(index: Int): Char { reads++; return text[index] }
			override fun subSequence(startIndex: Int, endIndex: Int) = text.subSequence(startIndex, endIndex)
		}
		val lexer = VeroScriptLexer()
		lexer.start(counted)
		assertEquals(VeroScriptTokens.lineComment, lexer.tokenType)
		assertEquals(text.length, lexer.tokenEnd)
		assertTrue("Continued comment scanning must remain linear", reads < text.length * 8)
	}

	@Test fun constexprKeepsKeywordIdentifierAndLiteralBoundaries() {
		val text = "constexpr int limit = 3; int constexprValue; int Constexpr; string text = \"constexpr\"; // constexpr"
		val tokens = scan(text)
		fun type(spelling: String) = tokens.single { text.substring(it.start, it.end) == spelling }.type
		assertEquals(VeroScriptTokens.keyword, type("constexpr"))
		assertEquals(VeroScriptTokens.identifier, type("constexprValue"))
		assertEquals(VeroScriptTokens.identifier, type("Constexpr"))
		assertEquals(VeroScriptTokens.string, type("\"constexpr\""))
		assertEquals(VeroScriptTokens.lineComment, type("// constexpr"))
	}

	@Test fun documentationSignaturesHighlightNamesWithoutChangingSourceTokens() {
		val text = "bool std::algorithm::binary_search(array<T> values, T value);\n" +
			"bool (array<T> values, T value)\nvoid print(any value);"
		val lexer = VeroScriptDocumentationLexer()
		fun scanFrom(offset: Int): List<Pair<String, IElementType>> {
			lexer.start(text, offset, text.length, 0)
			val result = mutableListOf<Pair<String, IElementType>>()
			while (lexer.tokenType != null) {
				result += text.substring(lexer.tokenStart, lexer.tokenEnd) to lexer.tokenType!!
				lexer.advance()
			}
			return result
		}
		val tokens = scanFrom(0)
		assertEquals(listOf("std", "algorithm"), tokens.filter { it.second == VeroScriptDocumentationTokens.qualifier }.map { it.first })
		assertEquals(listOf("binary_search", "print"), tokens.filter { it.second == VeroScriptDocumentationTokens.function }.map { it.first })
		assertEquals(listOf("T", "T", "T", "T", "any"), tokens.filter { it.second == VeroScriptDocumentationTokens.type }.map { it.first })
		assertTrue(tokens.filter { it.first == "values" || it.first == "value" }.all { it.second == VeroScriptTokens.identifier })
		var offset = 0
		tokens.forEachIndexed { index, token ->
			assertEquals("Restart at $offset", tokens.drop(index), scanFrom(offset))
			offset += token.first.length
		}
		assertEquals(scan(text).map { it.type }.count { it == VeroScriptTokens.identifier },
			tokens.count { it.second == VeroScriptTokens.identifier || it.second in setOf(
				VeroScriptDocumentationTokens.type, VeroScriptDocumentationTokens.function, VeroScriptDocumentationTokens.qualifier) })
	}

	@Test fun documentationClassificationPreservesLiteralsCommentsAndComparisons() {
		val text = "left < right > limit; print(\"std::fake(array<T>)\"); /* fake() */ array<map<string, T>> values; fn<int(int argument)> callback;"
		val lexer = VeroScriptDocumentationLexer()
		lexer.start(text)
		val classified = mutableListOf<Pair<String, IElementType>>()
		while (lexer.tokenType != null) {
			classified += text.substring(lexer.tokenStart, lexer.tokenEnd) to lexer.tokenType!!
			lexer.advance()
		}
		for (name in listOf("left", "right", "limit", "values", "argument", "callback")) {
			assertEquals(VeroScriptTokens.identifier, classified.single { it.first == name }.second)
		}
		assertEquals(VeroScriptDocumentationTokens.type, classified.single { it.first == "T" }.second)
		assertEquals(VeroScriptTokens.string, classified.single { it.first.startsWith('"') }.second)
		assertEquals(VeroScriptTokens.blockComment, classified.single { it.first.startsWith("/*") }.second)
	}

	@Test fun ownershipSignatureTypesRemainSoftNamesInSource() {
		val text = "std::weak<std::shared<domain::Point>> observer; handle<Point> owner; shared(value); int Handle;"
		val lexer = VeroScriptDocumentationLexer()
		lexer.start(text)
		val tokens = mutableListOf<Pair<String, IElementType>>()
		while (lexer.tokenType != null) {
			tokens += text.substring(lexer.tokenStart, lexer.tokenEnd) to lexer.tokenType!!
			lexer.advance()
		}
		for (name in listOf("weak", "shared", "handle", "Point")) {
			assertTrue(name, tokens.any { it.first == name && it.second == VeroScriptDocumentationTokens.type })
		}
		for (name in listOf("observer", "owner", "value", "Handle")) {
			assertEquals(name, VeroScriptTokens.identifier, tokens.single { it.first == name }.second)
		}
		assertTrue(tokens.any { it.first == "shared" && it.second == VeroScriptDocumentationTokens.function })
		val sourceTokens = scan(text)
		for (name in listOf("weak", "shared", "handle", "Handle")) {
			assertTrue(sourceTokens.filter { text.substring(it.start, it.end) == name }
				.all { it.type == VeroScriptTokens.identifier })
		}
	}

	private data class Token(val start: Int, val end: Int, val type: IElementType, val state: Int)
	private fun scan(text: String, offset: Int = 0, state: Int = 0): List<Token> {
		val lexer = VeroScriptLexer()
		lexer.start(text, offset, text.length, state)
		val result = mutableListOf<Token>()
		while (lexer.tokenType != null) {
			assertTrue("Lexer must always advance", lexer.tokenEnd > lexer.tokenStart)
			result += Token(lexer.tokenStart, lexer.tokenEnd, lexer.tokenType!!, lexer.state)
			lexer.advance()
		}
		return result
	}

	@Test fun multilineRawStringsAndCommentsRestartAtEveryToken() {
		val text = """#define X 1
            /* multiline
               comment */
            string s = r"raw \n
            second line";
            wstring w = L"wide";
            int64 n = 0xFFFF;
            double d = .5e-2;
            // comment
        """.trimIndent()
		val tokens = scan(text)
		tokens.forEachIndexed { index, token -> assertEquals(tokens.drop(index), scan(text, token.start, token.state)) }
		assertTrue(tokens.any { it.type == VeroScriptTokens.string && text.substring(it.start, it.end).startsWith("r\"") &&
			text.substring(it.start, it.end).contains('\n') })
		assertTrue(tokens.any { it.type == VeroScriptTokens.number && text.substring(it.start, it.end) == ".5e-2" })
	}

	@Test fun malformedInputStillConsumesEveryCharacter() {
		for (text in listOf("/* unclosed", "r\"raw\nend", "\"escaped\\", "'a", "\uD83D\uDE00 int x;", "")) {
			val tokens = scan(text)
			assertEquals(text.length, tokens.lastOrNull()?.end ?: 0)
		}
	}

	@Test fun numberBoundariesKeepHexadecimalArithmeticAndMemberNamesSeparate() {
		val text = "0xFE+2 0xDEAD-1 0b101u64 .5e-2 42i64 1.0.method 1_000UL"
		val tokens = scan(text)
		assertEquals(listOf("0xFE", "2", "0xDEAD", "1", "0b101u64", ".5e-2", "42i64", "1.0", "1_000UL"),
			tokens.filter { it.type == VeroScriptTokens.number }.map { text.substring(it.start, it.end) })
		assertEquals(listOf("+", "-", "."), tokens.filter { it.type == VeroScriptTokens.operator }.map { text.substring(it.start, it.end) })
		assertEquals("method", tokens.single { it.type == VeroScriptTokens.identifier }.let { text.substring(it.start, it.end) })
		val lexer = VeroScriptLexer()
		lexer.start("123456", 0, 3, 0)
		assertEquals(3, lexer.tokenEnd)
	}
}

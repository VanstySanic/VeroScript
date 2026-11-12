// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

package com.vanstysanic.veroscript.gui.jetbrains

import com.intellij.lexer.Lexer
import com.intellij.psi.tree.IElementType
import com.vanstysanic.veroscript.gui.jetbrains.language.*
import com.vanstysanic.veroscript.jetbrains.language.VeroScriptTokens
import org.junit.Assert.*
import org.junit.Test

class GuiLexerTest {
	private data class Token(val type: IElementType, val start: Int, val end: Int, val state: Int)
	private fun scan(lexer: Lexer, text: CharSequence, start: Int = 0, state: Int = 0, end: Int = text.length): List<Token> {
		lexer.start(text, start, end, state)
		return buildList {
			var offset = start
			while (lexer.tokenType != null) {
				assertEquals(offset, lexer.tokenStart)
				assertTrue(lexer.tokenEnd > offset)
				add(Token(lexer.tokenType!!, lexer.tokenStart, lexer.tokenEnd, lexer.state))
				offset = lexer.tokenEnd
				lexer.advance()
			}
			assertEquals(end, offset)
		}
	}
	private fun restart(factory: () -> Lexer, text: String) {
		val tokens = scan(factory(), text)
		tokens.forEachIndexed { index, token -> assertEquals("Restart at ${token.start}", tokens.drop(index),
			scan(factory(), text, token.start, token.state)) }
	}

	@Test fun markupPreservesNamespacesCdataAndRestartState() {
		val text = "<?xml version=\"1.0\"?><Widget bind:width=\"{ Count &lt; 3 }\" on:pointer-down='Change'>" +
			"<!-- <Fake/> --><style><![CDATA[:root { gap: 2px; }]]></style>" +
			"<script><![CDATA[void Act() { if (a < b) print(\"<Fake/>\"); }]]></script><Text/></Widget>"
		restart(::GuiMarkupLexer, text)
		val tokens = scan(GuiMarkupLexer(), text)
		assertEquals(listOf("Widget", "style", "style", "script", "script", "Text", "Widget"),
			tokens.filter { it.type == GuiTokens.tag }.map { text.substring(it.start, it.end) })
		assertEquals(listOf("bind:width", "on:pointer-down"), tokens.filter { it.type == GuiTokens.attribute }.map { text.substring(it.start, it.end) })
		assertTrue(tokens.any { it.type == GuiTokens.script && text.substring(it.start, it.end).contains("a < b") })
		val inline = "<Widget><script>// continued \\\nvoid ignored() {}\nint active;</script></Widget>"
		val highlighted = scan(GuiSyntaxHighlighter(true).highlightingLexer, inline)
		assertEquals("// continued \\\nvoid ignored() {}", highlighted.first { it.type == VeroScriptTokens.lineComment }.let {
			inline.substring(it.start, it.end)
		})
		assertTrue(highlighted.any { it.type == VeroScriptTokens.identifier && inline.substring(it.start, it.end) == "active" })
	}

	@Test fun styleRestartKeepsSelectorsPropertiesAndFunctionDepth() {
		val text = "Widget > .card:hover { --gap: 2px; gap: var(--gap, 8px); background-image: url(\"a;b.png\"); } /* end */"
		restart(::GuiStyleLexer, text)
		val tokens = scan(GuiStyleLexer(), text)
		assertEquals(listOf("--gap", "gap", "background-image"), tokens.filter { it.type == GuiTokens.property }.map { text.substring(it.start, it.end) })
		assertTrue(tokens.any { it.type == GuiTokens.styleString && text.substring(it.start, it.end) == "\"a;b.png\"" })
	}

	@Test fun boundedScansNeverReadPartialClosingMarkersOutsideTheirRange() {
		fun bounded(text: String, end: Int) = object : CharSequence {
			override val length = text.length
			override fun get(index: Int): Char {
				assertTrue("Read beyond lexer end: $index >= $end", index < end)
				return text[index]
			}
			override fun subSequence(startIndex: Int, endIndex: Int): CharSequence {
				assertTrue("Read beyond lexer end: $endIndex > $end", endIndex <= end)
				return text.subSequence(startIndex, endIndex)
			}
		}
		val markup = "<Widget><script><![CDATA[int value = 1;]]></script><style>:root { gap: 4px; }</style></Widget>"
		for (end in 0..markup.length) scan(GuiMarkupLexer(), bounded(markup, end), end = end)
		val style = ":root { /* comment */ background-image: url(\"image.png\"); }"
		for (end in 0..style.length) scan(GuiStyleLexer(), bounded(style, end), end = end)
	}

	@Test fun incompleteEditsAlwaysAdvanceAndRestart() {
		for (text in listOf("<", "</", "<Widget bind:", "<Widget Text=\"unterminated", "<!--", "<script><![CDATA[", "<style>Widget { gap: var(")) {
			restart(::GuiMarkupLexer, text)
		}
		for (text in listOf("", "/*", "Widget {", ":root { gap:", "Widget { background-image: url(\"", "Widget { opacity: .5;")) {
			restart(::GuiStyleLexer, text)
		}
	}

	@Test fun repeatedInlineBlocksAndCdataSegmentsRequireOnlyLinearReads() {
		for (text in listOf(
			"<script>int value = 1;</script><style>:root { gap: 2px; }</style>".repeat(300),
			"<script>" + "text<![CDATA[int value = 1;]]>".repeat(300) + "</script>",
		)) {
			var reads = 0
			val counted = object : CharSequence {
				override val length = text.length
				override fun get(index: Int): Char {
					reads++
					return text[index]
				}
				override fun subSequence(startIndex: Int, endIndex: Int) = text.subSequence(startIndex, endIndex)
			}
			val expected = scan(GuiMarkupLexer(), text)
			assertEquals(expected, scan(GuiMarkupLexer(), counted))
			// Counting source reads avoids a timing threshold that depends on CI hardware.
			assertTrue("$reads character reads for ${text.length} source characters", reads < text.length * 64)
		}
	}

	@Test fun catalogIncludesInheritedBindingsEventsAndOnlyStyleProperties() {
		val password = requireNotNull(GuiCatalog.find("password-box"))
		assertTrue(GuiCatalog.properties(password).any { it.name == "Value" && it.bindable })
		assertTrue(GuiCatalog.properties(password).any { it.name == "Width" && it.bindable })
		assertTrue(GuiCatalog.events(password).any { it.mark == "submitted" })
		assertTrue(GuiCatalog.events(password).any { it.mark == "pointer-down" })
		assertFalse(GuiCatalog.styleProperties.any { it.mark in setOf("text", "enabled", "hit-test") })
		assertTrue(GuiCatalog.styleProperties.any { it.mark == "columns" })
		assertNull(GuiCatalog.find("ApplicationDefinedControl"))
	}
}

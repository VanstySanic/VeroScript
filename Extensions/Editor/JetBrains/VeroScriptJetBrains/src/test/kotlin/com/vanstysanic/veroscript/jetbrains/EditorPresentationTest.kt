// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

package com.vanstysanic.veroscript.jetbrains

import com.intellij.codeInsight.editorActions.enter.EnterHandlerDelegate
import com.intellij.openapi.actionSystem.DataContext
import com.intellij.openapi.application.ApplicationManager
import com.intellij.openapi.command.WriteCommandAction
import com.intellij.openapi.command.undo.UndoManager
import com.intellij.openapi.editor.EditorFactory
import com.intellij.openapi.fileEditor.impl.text.TextEditorProvider
import com.intellij.openapi.project.ProjectManager
import com.intellij.openapi.util.Ref
import com.intellij.platform.backend.documentation.DocumentationTargetProvider
import com.intellij.psi.PsiDocumentManager
import com.intellij.psi.PsiFileFactory
import com.intellij.testFramework.TestApplicationManager
import com.vanstysanic.veroscript.jetbrains.language.VeroScriptBlockEnter
import com.vanstysanic.veroscript.jetbrains.language.VeroScriptEnterHandler
import com.vanstysanic.veroscript.jetbrains.language.VeroScriptFileType
import com.vanstysanic.veroscript.jetbrains.lsp.VeroScriptDocumentation
import com.vanstysanic.veroscript.jetbrains.lsp.VeroScriptDocumentationProvider
import com.vanstysanic.veroscript.jetbrains.lsp.VeroScriptDocumentationTarget
import com.vanstysanic.veroscript.jetbrains.settings.VeroScriptProjectSettings
import org.eclipse.lsp4j.Hover
import org.eclipse.lsp4j.MarkupContent
import org.junit.Assert.*
import org.junit.BeforeClass
import org.junit.Test
import java.lang.reflect.Proxy
import java.util.concurrent.CompletableFuture
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicInteger

class EditorPresentationTest {
	@Test fun builtinHoverMergesCategoriesForEveryLocaleAndRetainsDescriptions() {
		for (category in listOf("builtin type", "内置类型", "內建型別", "type intégré", "組み込み型", "기본 제공 형식")) {
			for (kind in listOf("keyword", "type alias", "class", "struct", "interface", "enum", "enum class")) {
				val source = "**` $kind array<T> `**\n\n$category\n\nDescription."
				val expected = "**$category** ` array<T> `\n\nDescription."
				assertEquals(expected, VeroScriptDocumentation.hoverMarkdown(source))
				assertEquals(expected, VeroScriptDocumentation.hoverMarkdown(expected))
			}
		}
		assertEquals("**keyword** ` return `\n\nReturn documentation.",
			VeroScriptDocumentation.hoverMarkdown("**` keyword return `**\n\nReturn documentation."))
	}

	@Test fun builtinSummariesCoverLaterEntriesWithoutChangingAliasesOrDocumentationExamples() {
		val example = "\n\n~~~text\n---\n\n**` type alias example `**\n\nbuiltin type\n~~~"
		val source = "**` type alias array `**\n\nbuiltin type\n\nContainer description.\n\n---\n\n" +
			"**` keyword int `**\n\nbuiltin type\n\nInteger description." + example
		val expected = "**builtin type** ` array `\n\nContainer description.\n\n---\n\n" +
			"**builtin type** ` int `\n\nInteger description." + example
		assertEquals(expected, VeroScriptDocumentation.hoverMarkdown(source))
		assertEquals(expected, VeroScriptDocumentation.hoverMarkdown(expected))
		val alias = "**` type alias Count `**\n\n```veroscript\nusing Count = int64;\n```\n\n" +
			"builtin type\n\n` defined at count.vs:1 `"
		assertEquals(alias.replace("**` type alias Count `**", "**type alias** ` Count `"),
			VeroScriptDocumentation.hoverMarkdown(alias))
		assertEquals("**function** ` user `\n\nbuiltin type",
			VeroScriptDocumentation.hoverMarkdown("**` function user `**\n\nbuiltin type"))
		assertEquals("**struct** ` optional<T> `\n\nBuilt-in symbol. ` value type `",
			VeroScriptDocumentation.hoverMarkdown("**` struct optional<T> `**\n\nBuilt-in symbol. ` value type `"))
	}

	@Test fun containerHoverRendersOneTypeCategoryAndLiteralGenericArguments() {
		val project = ProjectManager.getInstance().defaultProject
		val source = "**` type alias array<T> `**\n\nbuiltin type\n\nBuilt-in VeroScript type."
		val content = MarkupContent("markdown", source)
		val html = com.intellij.openapi.application.ReadAction.computeBlocking<String, RuntimeException> {
			VeroScriptDocumentationTarget.render(project, Hover(content))
		}
		val kit = javax.swing.text.html.HTMLEditorKit()
		val document = kit.createDefaultDocument()
		kit.read(java.io.StringReader(html), document, 0)
		val text = document.getText(0, document.length).replace('\u00a0', ' ')
		assertFalse(text, text.contains("type alias"))
		assertEquals(text, 1, Regex("builtin type").findAll(text).count())
		assertTrue(text, text.contains("array<T>"))
		assertTrue(text, text.contains("Built-in VeroScript type."))
		assertEquals(source, content.value)
	}

	@Test fun hoverPresentationPreservesCodeExamplesOverloadsBackticksAndUnknownFormats() {
		val body = "\n\n```veroscript\narray<T> collect(map<string, int64> values);\n```\n\n" +
			"~~~text\n---\n\n**` function example `**\n~~~\n\n[Guide](https://example.com)."
		val source = "**` function collect `**" + body
		val rendered = VeroScriptDocumentation.hoverMarkdown(source)
		assertEquals("**function** ` collect `" + body, rendered)
		assertEquals(rendered, VeroScriptDocumentation.hoverMarkdown(rendered))
		assertEquals("**function** ` a `\n\n---\n\n**method** ` C.a `",
			VeroScriptDocumentation.hoverMarkdown("**` function a `**\n\n---\n\n**` method C.a `**"))
		assertEquals("**macro** `` VALUE` ``", VeroScriptDocumentation.hoverMarkdown("**`` macro VALUE` ``**"))
		for (value in listOf("", "A future format.", "**` unknown int `**", "```veroscript\nint a;\n```"))
			assertEquals(value, VeroScriptDocumentation.hoverMarkdown(value))
	}

	@Test fun hoverHtmlUsesTheNativeRendererWithoutMutatingProtocolContents() {
		val project = ProjectManager.getInstance().defaultProject
		val content = MarkupContent("markdown", "**` keyword int `**\n\nbuiltin type\n\nBuilt-in VeroScript type.")
		val hover = Hover(content)
		val html = com.intellij.openapi.application.ReadAction.computeBlocking<String, RuntimeException> {
			VeroScriptDocumentationTarget.render(project, hover)
		}
		assertTrue(html, html.contains("builtin type"))
		assertFalse(html, html.contains("keyword int"))
		assertTrue(html, html.contains("Built-in VeroScript type."))
		assertEquals("**` keyword int `**\n\nbuiltin type\n\nBuilt-in VeroScript type.", content.value)
		val plain = com.intellij.openapi.application.ReadAction.computeBlocking<String, RuntimeException> {
			VeroScriptDocumentationTarget.render(project, Hover(MarkupContent("plaintext", "<T> & value")))
		}
		assertEquals("<pre>&lt;T&gt; &amp; value</pre>", plain)
	}

	@Test fun hoverRequestsStayOffTheUiThreadAndDiscardResponsesAfterAnEdit() {
		val project = ProjectManager.getInstance().defaultProject
		val document = EditorFactory.getInstance().createDocument("int value;")
		val file = com.intellij.testFramework.LightVirtualFile("hover.vs", VeroScriptFileType, document.text)
		val requested = CompletableFuture<Void>()
		val reply = CompletableFuture<Hover>()
		val calls = AtomicInteger()
		val service = proxy<org.eclipse.lsp4j.services.TextDocumentService> { method, arguments ->
			if (method == "hover") {
				assertFalse(ApplicationManager.getApplication().isDispatchThread)
				val params = arguments!![0] as org.eclipse.lsp4j.HoverParams
				assertEquals(org.eclipse.lsp4j.Position(0, 1), params.position)
				calls.incrementAndGet()
				requested.complete(null)
				reply
			}
			else null
		}
		val server = proxy<org.eclipse.lsp4j.services.LanguageServer> { method, _ ->
			if (method == "getTextDocumentService") service
			else null
		}
		val client = proxy<com.intellij.platform.lsp.api.LspClient> { method, arguments -> when (method) {
			"getProject" -> project
			"getState" -> com.intellij.platform.lsp.api.LspServerState.Running
			"getDocumentIdentifier" -> org.eclipse.lsp4j.TextDocumentIdentifier(file.url)
			"sendRequestSync" -> {
				@Suppress("UNCHECKED_CAST")
				val request = arguments!![1] as (org.eclipse.lsp4j.services.LanguageServer) -> CompletableFuture<Hover>
				request(server).get(5, TimeUnit.SECONDS)
			}
			else -> null
		} }
		val target = VeroScriptDocumentationTarget(client, file, document, document.modificationStamp,
			org.eclipse.lsp4j.Position(0, 1), "int")
		val pointer = target.createPointer()
		assertSame(target, pointer.dereference())
		val result = CompletableFuture.supplyAsync { resolve(target.computeDocumentation()) }
		requested.get(5, TimeUnit.SECONDS)
		ApplicationManager.getApplication().invokeAndWait {
			WriteCommandAction.runWriteCommandAction(project) { document.insertString(0, " ") }
		}
		reply.complete(Hover(MarkupContent("markdown", "**` keyword int `**\n\nbuiltin type")))
		assertNull(result.get(5, TimeUnit.SECONDS))
		assertNull(pointer.dereference())
		assertNull(resolve(target.computeDocumentation()))
		assertEquals(1, calls.get())
		val current = VeroScriptDocumentationTarget(client, file, document, document.modificationStamp,
			org.eclipse.lsp4j.Position(0, 1), "int")
		assertNotNull(resolve(current.computeDocumentation()))
		assertSame(current, current.createPointer().dereference())
		assertEquals(2, calls.get())
	}

	@Test fun emptyBlockEnterUsesHeaderIndentAndLeavesTheCaretInside() {
		for (header in listOf("void main()", "void t", "int value() const", "class Demo", "enum class Color",
			"public static class A final", "namespace demo", "if (ready)", "for (int i=0;i<3;++i)", "else", "try", "catch (any error)")) {
			val source = "    $header {|}"
			assertEquals("    $header\n    {\n        |\n    }", enter(source))
		}
		assertEquals("void main()\n{\n    |", enter("void main(){|"))
		assertEquals("\tvoid main()\r\n\t{\r\n\t\t|\r\n\t}\r\n", enter("\tvoid main(){|}\r\n", "\t"))
	}

	@Test fun emptyBlockEnterDoesNotTouchCommentsLiteralsDirectivesOrNonemptyBlocks() {
		for (source in listOf("// void main(){|}", "/* comment\nvoid main(){|}", "auto text=r\"first\nvoid main(){|}",
			"#define BLOCK void main(){|}", "#define BLOCK \\\nvoid main(){|}", "array<int> values {|}",
			"print(\"void main(){|}\");", "void main()\n{|}", "void main(){|print(1);}",
			"void main(){print(1);if(ready){|}", "void t(\n    int value){|}",
			"class C\n    : Base(){|}", "if (ready{|}")) {
			val offset = source.indexOf('|')
			assertNull(source, VeroScriptBlockEnter.plan(source.replace("|", ""), offset, "    "))
		}
		val large = " ".repeat(1024 * 1024) + "void main(){|}"
		assertNull(VeroScriptBlockEnter.plan(large.replace("|", ""), large.indexOf('|'), "    "))
	}

	@Test fun emptyBlockEnterDistinguishesContinuedCommentsFromCompletedDirectives() {
		for (eol in listOf("\n", "\r\n")) {
			for (prefix in listOf("// comment \\" + eol, "// comment \\" + eol + "ignored \\" + eol)) {
				val source = prefix + "void main(){|}"
				assertNull(source, VeroScriptBlockEnter.plan(source.replace("|", ""), source.indexOf('|'), "\t"))
			}
			val prefix = "// comment \\" + eol + "ignored" + eol
			assertEquals(prefix + "void main()" + eol + "{" + eol + "\t|" + eol + "}",
				enter(prefix + "void main(){|}" + eol, "\t").removeSuffix(eol))
			val directive = "#define VALUE \\ " + eol
			assertEquals(directive + "void main()" + eol + "{" + eol + "\t|" + eol + "}",
				enter(directive + "void main(){|}" + eol, "\t").removeSuffix(eol))
		}
	}

	@Test fun emptyBlockEnterBoundsTheCurrentLineSuffixWithoutScanningTheWholeFile() {
		val header = "void main(){"
		val oversized = object : CharSequence {
			override val length = 2 * 1024 * 1024
			override fun get(index: Int): Char {
				assertTrue("Enter analysis read beyond its budget", index < 1024 * 1024)
				return if (index < header.length) header[index]
				else ' '
			}
			override fun subSequence(startIndex: Int, endIndex: Int): CharSequence =
				error("An oversized current line must be rejected before materializing its contents")
		}
		assertNull(VeroScriptBlockEnter.plan(oversized, header.length, "    "))
		val largeFile = "void main(){|}\n" + " ".repeat(1024 * 1024)
		assertEquals("void main()\n{\n    |\n}\n" + " ".repeat(1024 * 1024), enter(largeFile))
		// A caller with an offset inside a CRLF must fall back instead of forming a reversed range.
		assertNull(VeroScriptBlockEnter.plan("void main(){\r\n}", header.length + 1, "    "))
	}

	@Test fun emptyBlockEnterPreservesLongValidWhitespaceWithoutRegexBacktracking() {
		val gap = " ".repeat(32 * 1024)
		assertEquals("void${gap}main()\n{\n    |\n}", enter("void${gap}main()${gap}{|}"))
		for (source in listOf("\u000Bvoid main(){|}", "\u000Cvoid main(){|}", "void\rmain(){|}",
			"void\u0085main(){|}", "void\u2028main(){|}", "void\u2029main(){|}")) {
			assertNull(source, VeroScriptBlockEnter.plan(source.replace("|", ""), source.indexOf('|'), "    "))
		}
	}

	@Test fun enterExtensionUsesOneUndoableCommandAndRespectsTheProjectSetting() {
		ApplicationManager.getApplication().invokeAndWait {
			val project = ProjectManager.getInstance().defaultProject
			val settings = VeroScriptProjectSettings.get(project).state
			val original = settings.bracesOnNewLine
			val file = PsiFileFactory.getInstance(project).createFileFromText("enter.vs", VeroScriptFileType, "void t(){}", 0L, true)
			val document = requireNotNull(PsiDocumentManager.getInstance(project).getDocument(file))
			val factory = EditorFactory.getInstance()
			val editor = factory.createEditor(document, project)
			// The default project is not open; its documents use the global command manager.
			val undo = UndoManager.getGlobalInstance()
			try {
				assertTrue(EnterHandlerDelegate.EP_NAME.extensionList.any { it is VeroScriptEnterHandler })
				assertTrue(DocumentationTargetProvider.EP_NAME.extensionList.any { it is VeroScriptDocumentationProvider })
				editor.caretModel.moveToOffset(9)
				settings.bracesOnNewLine = false
				assertEquals(EnterHandlerDelegate.Result.Continue, VeroScriptEnterHandler().preprocessEnter(
					file, editor, Ref(9), Ref(0), DataContext.EMPTY_CONTEXT, null))
				settings.bracesOnNewLine = true
				WriteCommandAction.runWriteCommandAction(null) {
					assertEquals(EnterHandlerDelegate.Result.Stop, VeroScriptEnterHandler().preprocessEnter(
						file, editor, Ref(9), Ref(0), DataContext.EMPTY_CONTEXT, null))
				}
				assertEquals("void t()\n{\n    \n}", document.text)
				assertEquals(document.text.indexOf("    ") + 4, editor.caretModel.offset)
				val fileEditor = TextEditorProvider.getInstance().getTextEditor(editor)
				assertTrue(undo.isUndoAvailable(fileEditor))
				undo.undo(fileEditor)
				assertEquals("void t(){}", document.text)
			}
			finally
			{
				settings.bracesOnNewLine = original
				factory.releaseEditor(editor)
			}
		}
	}

	private fun enter(source: String, unit: String = "    "): String {
		val offset = source.indexOf('|')
		val text = source.replace("|", "")
		val edit = requireNotNull(VeroScriptBlockEnter.plan(text, offset, unit)) { source }
		val result = text.replaceRange(edit.start, edit.end, edit.text)
		return result.substring(0, edit.caret) + "|" + result.substring(edit.caret)
	}

	companion object {
		@JvmStatic @BeforeClass fun initializeApplication() { TestApplicationManager.getInstance() }
		private fun resolve(result: com.intellij.platform.backend.documentation.DocumentationResult):
			com.intellij.platform.backend.documentation.DocumentationResult.Documentation? {
			// Resolve the SDK's asynchronous result exactly as its documentation host does.
			@Suppress("UNCHECKED_CAST")
			val supplier = result.javaClass.getMethod("getSupplier").invoke(result) as
				suspend () -> com.intellij.platform.backend.documentation.DocumentationResult.Documentation?
			return kotlinx.coroutines.runBlocking { supplier() }
		}
		private inline fun <reified T> proxy(noinline call: (String, Array<out Any?>?) -> Any?): T =
			Proxy.newProxyInstance(T::class.java.classLoader, arrayOf(T::class.java)) { instance, method, arguments ->
				when (method.name) {
					"hashCode" -> System.identityHashCode(instance)
					"equals" -> instance === arguments?.firstOrNull()
					"toString" -> T::class.java.simpleName
					else -> call(method.name, arguments)
				}
			} as T
	}
}

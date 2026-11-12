// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

package com.vanstysanic.veroscript.jetbrains

import com.intellij.lang.LanguageParserDefinitions
import com.intellij.openapi.fileTypes.FileTypeManager
import com.intellij.openapi.editor.DefaultLanguageHighlighterColors as Colors
import com.intellij.testFramework.TestApplicationManager
import com.vanstysanic.veroscript.jetbrains.language.VeroScriptFileType
import com.vanstysanic.veroscript.jetbrains.language.VeroScriptLanguage
import org.junit.Assert.*
import org.junit.BeforeClass
import org.junit.Test
import java.util.Locale
import java.util.ResourceBundle

/** Loads extensions in the actual Rider application without creating a fake .NET solution. */
class PluginRegistrationTest {
	@Test fun runConfigurationValidationPropagatesCancellation() {
		val canceled = com.intellij.openapi.progress.ProcessCanceledException()
		val project = java.lang.reflect.Proxy.newProxyInstance(javaClass.classLoader,
			arrayOf(com.intellij.openapi.project.Project::class.java)) { instance, method, args ->
			when (method.name) {
				"getBasePath" -> throw canceled
				"hashCode" -> System.identityHashCode(instance)
				"equals" -> instance === args?.firstOrNull()
				else -> null
			}
		} as com.intellij.openapi.project.Project
		val type = com.vanstysanic.veroscript.jetbrains.run.VeroScriptConfigurationType()
		val configuration = com.vanstysanic.veroscript.jetbrains.run.VeroScriptRunConfiguration(project,
			type.configurationFactories.single(), "Cancellation")
		val failure = assertThrows(com.intellij.openapi.progress.ProcessCanceledException::class.java) {
			configuration.checkConfiguration()
		}
		assertSame(canceled, failure)
	}

	@Test fun bothAttachModesNormalizeHostsAndRejectEmbeddedWhitespace() {
		val settings = com.vanstysanic.veroscript.jetbrains.settings.VeroScriptProjectSettings()
		val root = java.nio.file.Path.of(".").toAbsolutePath().normalize()
		val project = java.lang.reflect.Proxy.newProxyInstance(javaClass.classLoader,
			arrayOf(com.intellij.openapi.project.Project::class.java)) { instance, method, args ->
			when (method.name) {
				"getBasePath" -> root.toString()
				"getService" -> if (args?.firstOrNull() == settings.javaClass) settings
				else null
				"hashCode" -> System.identityHashCode(instance)
				"equals" -> instance === args?.get(0)
				else -> null
			}
		} as com.intellij.openapi.project.Project
		val type = com.vanstysanic.veroscript.jetbrains.run.VeroScriptConfigurationType()
		val configuration = com.vanstysanic.veroscript.jetbrains.run.VeroScriptRunConfiguration(project,
			type.configurationFactories.single(), "Attach validation")
		configuration.options.mode = "attach"
		for (mode in listOf("program", "domain")) {
			configuration.options.attachMode = mode
			configuration.options.host = " 127.0.0.1 "
			assertEquals("127.0.0.1", configuration.resolve().host)
			for (host in listOf(" ", "local host", "local\nhost", "host\u0000")) {
				configuration.options.host = host
				val error = assertThrows(IllegalArgumentException::class.java) { configuration.resolve() }
				assertEquals(VeroScriptBundle.message("run.invalidHost"), error.message)
			}
		}
	}

	@Test fun runJitBackendPathsFollowWorkingDirectoryAndProjectMacro() {
		val root = java.nio.file.Files.createTempDirectory("vero run paths 空格 ")
		val working = java.nio.file.Files.createDirectory(root.resolve("work"))
		val program = java.nio.file.Files.writeString(root.resolve("main.vs"), "void main() {}")
		val settings = com.vanstysanic.veroscript.jetbrains.settings.VeroScriptProjectSettings()
		val local = com.vanstysanic.veroscript.jetbrains.settings.VeroScriptLocalSettings().apply {
			state.runtimeHome = root.toString()
		}
		val project = java.lang.reflect.Proxy.newProxyInstance(javaClass.classLoader,
			arrayOf(com.intellij.openapi.project.Project::class.java)) { instance, method, args ->
			when (method.name) {
				"getBasePath" -> root.toString()
				"getService" -> when (args?.firstOrNull()) {
					settings.javaClass -> settings
					local.javaClass -> local
					else -> error("Unexpected project service")
				}
				"hashCode" -> System.identityHashCode(instance)
				"equals" -> instance === args?.firstOrNull()
				"isDisposed" -> false
				else -> null
			}
		} as com.intellij.openapi.project.Project
		try {
			val type = com.vanstysanic.veroscript.jetbrains.run.VeroScriptConfigurationType()
			val config = com.vanstysanic.veroscript.jetbrains.run.VeroScriptRunConfiguration(project,
				type.configurationFactories.single(), "Paths")
			config.options.apply {
				this.program = "${'$'}PROJECT_DIR${'$'}/main.vs"
				workingDirectory = "work"
				jitBackendPath = "../jit 后端"
			}
			assertEquals(root.resolve("jit 后端"), config.resolve().jitBackendPath)
			config.options.jitBackendPath = "${'$'}PROJECT_DIR${'$'}/jit 后端"
			assertEquals(root.resolve("jit 后端"), config.resolve().jitBackendPath)
			assertEquals("${'$'}PROJECT_DIR${'$'}/jit 后端", config.options.jitBackendPath)
			config.options.jitBackendPath = "unused\u0000backend"
			config.checkConfiguration()
			assertNull(config.resolve(includeJitBackend = false).jitBackendPath)
			assertThrows(java.nio.file.InvalidPathException::class.java) { config.resolve() }
			config.options.mode = "check"
			assertNull(config.resolve().jitBackendPath)
		}
		finally
		{
			java.nio.file.Files.deleteIfExists(program)
			java.nio.file.Files.deleteIfExists(working)
			java.nio.file.Files.deleteIfExists(root)
		}
	}

	@Test fun semanticHighlightingUsesServerReadonlyModifierForFields() {
		val project = com.intellij.openapi.project.ProjectManager.getInstance().defaultProject
		val descriptor = com.vanstysanic.veroscript.jetbrains.lsp.VeroScriptLspDescriptor(project,
			com.vanstysanic.veroscript.jetbrains.runtime.RuntimeLayout(java.nio.file.Path.of(".")))
		val support = descriptor.lspCustomization.semanticTokensCustomizer as
			com.intellij.platform.lsp.api.customization.LspSemanticTokensSupport
		assertTrue("The client must advertise the standard readonly modifier", "readonly" in support.tokenModifiers)
		assertSame(Colors.CONSTANT, support.getTextAttributesKey("property", listOf("readonly")))
		assertSame(Colors.CONSTANT, support.getTextAttributesKey("property", listOf("static", "readonly")))
		assertSame(Colors.INSTANCE_FIELD, support.getTextAttributesKey("property", emptyList()))
		assertSame(Colors.INSTANCE_FIELD, support.getTextAttributesKey("property", listOf("unknown")))
		assertSame(Colors.LOCAL_VARIABLE, support.getTextAttributesKey("variable", emptyList()))
		assertSame(Colors.CONSTANT, support.getTextAttributesKey("constant", emptyList()))
	}

	@Test fun hoverSignatureUsesThemeColorsForNamespacesFunctionsAndGenericTypes() {
		val colors = com.intellij.openapi.editor.colors.EditorColorsManager.getInstance()
		val original = colors.globalScheme
		val scheme = original.clone() as com.intellij.openapi.editor.colors.EditorColorsScheme
		scheme.setAttributes(Colors.CLASS_NAME, com.intellij.openapi.editor.markup.TextAttributes(
			java.awt.Color(180, 80, 230), null, null, null, java.awt.Font.PLAIN))
		scheme.setAttributes(Colors.FUNCTION_DECLARATION, com.intellij.openapi.editor.markup.TextAttributes(
			java.awt.Color(20, 190, 130), null, null, null, java.awt.Font.PLAIN))
		try {
			colors.setGlobalScheme(scheme)
			val project = com.intellij.openapi.project.ProjectManager.getInstance().defaultProject
			val source = java.nio.file.Files.createTempFile("veroscript-source-highlighting-", ".vs")
			try {
				java.nio.file.Files.writeString(source, "bool std::algorithm::binary_search(array<T> values, T value);")
				val file = requireNotNull(com.intellij.openapi.vfs.LocalFileSystem.getInstance().refreshAndFindFileByNioFile(source))
				val lexer = requireNotNull(com.intellij.openapi.fileTypes.SyntaxHighlighterFactory.getSyntaxHighlighter(
					VeroScriptLanguage, project, file)).highlightingLexer
				assertTrue("Physical source must keep the ordinary lexer", lexer is com.vanstysanic.veroscript.jetbrains.language.VeroScriptLexer)
			}
			finally
			{
				java.nio.file.Files.deleteIfExists(source)
			}
			val html = com.intellij.openapi.application.ReadAction.computeBlocking<String, RuntimeException> {
				com.intellij.markdown.utils.doc.DocMarkdownToHtmlConverter.convert(project,
					"```veroscript\nbool std::algorithm::binary_search(array<T> values, T value);\n```", VeroScriptLanguage)
			}
			val kit = javax.swing.text.html.HTMLEditorKit()
			val document = kit.createDefaultDocument() as javax.swing.text.html.HTMLDocument
			kit.read(java.io.StringReader(html), document, 0)
			val text = document.getText(0, document.length)
			fun foreground(name: String) = javax.swing.text.StyleConstants.getForeground(
				document.getCharacterElement(text.indexOf(name).also { assertTrue(html, it >= 0) }).attributes)
			assertNotEquals(html, foreground("binary_search"), foreground("values"))
			assertNotEquals(html, foreground("std"), foreground("values"))
			assertEquals(html, foreground("std"), foreground("algorithm"))
			assertEquals(html, foreground("std"), foreground("T"))
		}
		finally
		{
			colors.setGlobalScheme(original)
		}
	}

	@Test fun hoverMarkdownRendersAsStructuredDocumentation() {
		val markdown = "**` function print `**\n\n" +
			"```veroscript\nvoid print(any value);\nvoid print(string fmt, ...);\n```\n\n" +
			"*Built-in symbol.*\n\n` (delegate.vs; delegate<R>) `"
		val project = com.intellij.openapi.project.ProjectManager.getInstance().defaultProject
		val html = com.intellij.openapi.application.ReadAction.computeBlocking<String, RuntimeException> {
			com.intellij.markdown.utils.doc.DocMarkdownToHtmlConverter.convert(project, markdown, VeroScriptLanguage)
		}
		assertTrue(html, Regex("<(b|strong)>").containsMatchIn(html))
		assertTrue(html, html.contains("<pre"))
		val htmlKit = javax.swing.text.html.HTMLEditorKit()
		val document = htmlKit.createDefaultDocument()
		htmlKit.read(java.io.StringReader(html), document, 0)
		val text = document.getText(0, document.length).replace('\u00a0', ' ')
		assertTrue(html, text.contains("void print(any value);"))
		assertTrue(html, text.contains("void print(string fmt, ...);"))
		assertTrue(html, Regex("<br\\s*/?>").containsMatchIn(html))
		assertTrue(html, text.contains("Built-in symbol."))
		assertTrue(html, text.contains("delegate<R>"))
		assertFalse(html, text.contains("```"))
	}

	@Test fun fileTypeAndParserAreRegistered() {
		assertSame(VeroScriptFileType, FileTypeManager.getInstance().getFileTypeByFileName("hello.vs"))
		assertNotSame(VeroScriptFileType, FileTypeManager.getInstance().getFileTypeByFileName("library.vslib"))
		assertNotNull(LanguageParserDefinitions.INSTANCE.forLanguage(VeroScriptLanguage))
		val providers = com.intellij.openapi.extensions.ExtensionPointName.create<com.intellij.platform.lsp.api.LspIntegrationProvider>(
			"com.intellij.platform.lsp.integrationProvider").extensionList
		assertTrue(providers.any { it is com.vanstysanic.veroscript.jetbrains.lsp.VeroScriptLspProvider })
		val descriptors = com.vanstysanic.veroscript.jetbrains.language.VeroScriptColorSettingsPage().attributeDescriptors
		assertTrue(descriptors.all { it.key.externalName.startsWith("VEROSCRIPT_") })
	}

	@Test fun allSixBundlesProvideTheSameKeys() {
		val settings = com.vanstysanic.veroscript.jetbrains.settings.VeroScriptProjectSettings()
		assertEquals(com.intellij.DynamicBundle.getLocale().toLanguageTag(), settings.locale())
		settings.state.locale = "ja-JP"
		assertEquals("ja-JP", settings.locale())
		val name = "Localization.messages.VeroScriptBundle"
		val loader = javaClass.classLoader
		val keys = ResourceBundle.getBundle(name, Locale.ROOT, loader).keySet()
		for (tag in listOf("en-US", "zh-CN", "zh-TW", "ja-JP", "ko-KR", "fr-FR")) {
			val bundle = ResourceBundle.getBundle(name, Locale.forLanguageTag(tag), loader)
			assertEquals(keys, bundle.keySet())
			keys.forEach { assertFalse(bundle.getString(it).isBlank()) }
			val suffix = if (tag == "en-US") ""
			else "_" + tag.replace('-', '_')
			val own = java.util.Properties()
			requireNotNull(loader.getResourceAsStream("Localization/messages/VeroScriptBundle$suffix.properties"))
				.bufferedReader(Charsets.UTF_8).use { own.load(it) }
			assertEquals(keys, own.stringPropertyNames())
			// Check formatting, not just placeholder text parity: quoting can hide arguments.
			for (key in own.stringPropertyNames()) {
				val pattern = own.getProperty(key)
				val indexes = Regex("\\{(\\d+)\\}").findAll(pattern).map { it.groupValues[1].toInt() }.toSet()
				if (indexes.isEmpty()) continue
				val arguments = Array(indexes.max() + 1) { "opaque-argument-$it" }
				val formatted = java.text.MessageFormat(pattern, Locale.forLanguageTag(tag)).format(arguments)
				indexes.forEach { assertTrue("$tag: $key: $formatted", formatted.contains(arguments[it])) }
			}
		}
	}

	companion object {
		@JvmStatic @BeforeClass fun initializeApplication() { TestApplicationManager.getInstance() }
	}
}

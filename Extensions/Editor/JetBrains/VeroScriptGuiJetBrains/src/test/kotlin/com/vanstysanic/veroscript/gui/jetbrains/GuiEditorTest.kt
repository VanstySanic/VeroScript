// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

package com.vanstysanic.veroscript.gui.jetbrains

import com.intellij.testFramework.fixtures.BasePlatformTestCase
import com.intellij.testFramework.fixtures.TempDirTestFixture
import com.intellij.testFramework.fixtures.impl.TempDirTestFixtureImpl
import com.intellij.testFramework.LightProjectDescriptor
import com.intellij.testFramework.TestApplicationManager
import com.intellij.codeInsight.CodeInsightSettings
import com.intellij.ide.impl.OpenProjectTask
import com.intellij.openapi.application.ApplicationManager
import com.jetbrains.rd.ide.model.RdOpenSolution
import com.jetbrains.rider.projectView.SolutionDescriptionFactory
import com.jetbrains.rider.projectView.SolutionInitializerService
import com.intellij.openapi.command.WriteCommandAction
import com.intellij.psi.ElementManipulators
import com.intellij.psi.PsiDocumentManager
import com.intellij.psi.PsiManager
import com.intellij.psi.impl.source.resolve.reference.impl.providers.FileReference
import com.intellij.psi.util.PsiTreeUtil
import com.intellij.openapi.vfs.LocalFileSystem
import com.vanstysanic.veroscript.gui.jetbrains.language.*
import java.nio.file.Files
import java.nio.file.Path

/** Editor fixtures exercise the GUI client without starting a native Runtime. */
class GuiEditorTest : BasePlatformTestCase() {
	override fun setUp()
	{
		TestApplicationManager.getInstance()
		// Keep the SDK damage guard while comparing against Rider's initialized editor defaults.
		setDefaultCodeInsightSettings(CodeInsightSettings.getInstance().clone())
		super.setUp()
	}

	// Rider solution scopes map files to NIO paths; an in-memory temp:// root is insufficient.
	override fun createTempDirTestFixture(): TempDirTestFixture = TempDirTestFixtureImpl()

	override fun getProjectDescriptor(): LightProjectDescriptor = object : LightProjectDescriptor()
	{
		private lateinit var projectDirectory: Path

		override fun generateProjectPath(): Path = super.generateProjectPath().also { projectDirectory = it }

		override fun getOpenProjectOptions(): OpenProjectTask = super.getOpenProjectOptions().copy(beforeInit = { project ->
			// Rider requires its normal folder solution before registering client project sessions.
			// The component store does not expose project name/path during beforeInit.
			val description = SolutionDescriptionFactory.virtual(projectDirectory.fileName.toString(), projectDirectory.toString(), emptyList())
			ApplicationManager.getApplication().getService(SolutionInitializerService::class.java)
				.initSolution(project, RdOpenSolution(description, false))
		})
	}

	private fun accept(name: String, character: Char) {
		val items = myFixture.completeBasic().orEmpty()
		val lookup = requireNotNull(myFixture.lookup) { "Expected GUI completion lookup" }
		lookup.currentItem = items.first { it.lookupString == name }
		myFixture.finishLookup(character)
	}

	fun testEqualsAcceptanceInsertsOneAssignmentAndKeepsTheCaretInsideQuotes() {
		myFixture.configureByText("page.vswml", "<Widget <caret>/>")
		accept("Width", '=')
		myFixture.checkResult("<Widget Width=\"<caret>\"/>")
		myFixture.configureByText("page.vswml", "<Widget <caret>=\"12px\"/>")
		accept("Width", '=')
		myFixture.checkResult("<Widget Width=\"<caret>12px\"/>")
	}

	fun testColonAcceptancePreservesExistingStyleValues() {
		myFixture.configureByText("page.vswcss", ":root { <caret> }")
		accept("gap", ':')
		myFixture.checkResult(":root { gap: <caret> }")
		myFixture.configureByText("page.vswcss", ":root { <caret>: 6px; }")
		accept("gap", ':')
		myFixture.checkResult(":root { gap: <caret>6px; }")
	}

	fun testTabReplacesCompleteHyphenatedStyleAndEventNames() {
		myFixture.configureByText("page.vswcss", ":root { back<caret>ground-image: url(\"a.png\"); }")
		accept("background-image", '\t')
		myFixture.checkResult(":root { background-image<caret>: url(\"a.png\"); }")
		myFixture.configureByText("page.vswml", "<Slider on:<caret>value-changed=\"Change\"/>")
		accept("on:value-changed", '\t')
		myFixture.checkResult("<Slider on:value-changed<caret>=\"Change\"/>")
	}

	fun testStyleStringsCommentsAndPunctuationKeepTheirOwnLanguage() {
		val file = myFixture.configureByText("page.vswcss", "/* comment */ :root { background-image: url(\"image.png\"); }")
		for (offset in listOf(2, file.text.indexOf("image.png"), file.text.indexOf(';'))) {
			assertSame(GuiStyleLanguage, file.findElementAt(offset)?.language)
		}
	}

	fun testDocumentationDoesNotClaimSelectorsAndRejectsEditedSnapshots() {
		val file = myFixture.configureByText("page.vswcss", "width { width: 4px; }<caret>")
		val provider = GuiDocumentation()
		assertTrue(provider.documentationTargets(file, 1).isEmpty())
		val target = provider.documentationTargets(file, file.text.indexOf("width:")).single()
		val pointer = target.createPointer()
		assertNotNull(target.computeDocumentation())
		myFixture.type(' ')
		PsiDocumentManager.getInstance(project).commitAllDocuments()
		assertNull(pointer.dereference())
		assertNull(target.computeDocumentation())
	}

	fun testPathReferencesCanUpdateFileAndFolderNames() {
		myFixture.addFileToProject("ui/styles/page.vswcss", ":root { gap: 4px; }")
		val file = myFixture.addFileToProject("ui/page.vswml", "<Widget><style src=\"styles/page.vswcss\"/></Widget>")
		WriteCommandAction.runWriteCommandAction(project) {
			requireNotNull(file.findReferenceAt(file.text.indexOf("page.vswcss") + 2)).handleElementRename("other.vswcss")
			requireNotNull(file.findReferenceAt(file.text.indexOf("styles") + 2)).handleElementRename("theme")
		}
		assertEquals("<Widget><style src=\"theme/other.vswcss\"/></Widget>", file.text)
	}

	fun testLiteralEditsEscapeMarkupWithoutCompletingAnUnfinishedQuote() {
		val file = myFixture.configureByText("page.vswml", "<Widget><style src='old.vswcss'/></Widget>")
		WriteCommandAction.runWriteCommandAction(project) {
			val literal = requireNotNull(PsiTreeUtil.findChildOfType(file, GuiStringLiteral::class.java))
			val manipulator = requireNotNull(ElementManipulators.getManipulator(literal))
			manipulator.handleContentChange(literal, "a&b'c.vswcss")
		}
		myFixture.checkResult("<Widget><style src='a&amp;b&apos;c.vswcss'/></Widget>")
		val unfinished = myFixture.configureByText("unfinished.vswml", "<import src=\"old.vs")
		WriteCommandAction.runWriteCommandAction(project) {
			val literal = requireNotNull(PsiTreeUtil.findChildOfType(unfinished, GuiStringLiteral::class.java))
			requireNotNull(ElementManipulators.getManipulator(literal)).handleContentChange(literal, "new.vs")
		}
		myFixture.checkResult("<import src=\"new.vs")
	}

	fun testMarkupAttributeCompletionIncludesBindingsAndInheritedEvents() {
		myFixture.configureByText("page.vswml", "<Slider <caret>/>")
		val items = myFixture.completeBasic().orEmpty().map { it.lookupString }.toSet()
		assertTrue("bind:Width" in items)
		assertTrue("bind:value" in items)
		assertTrue("on:ValueChanged" in items)
		assertTrue("on:pointer-down" in items)
		assertTrue("id" in items)
	}

	fun testCompositeTagsAreAvailableInMarkupAndStyleSelectors() {
		for ((file, text) in listOf("page.vswml" to "<<caret>", "page.vswcss" to "<caret> {}")) {
			myFixture.configureByText(file, text)
			val names = myFixture.completeBasic().orEmpty().map { it.lookupString }.toSet()
			for (name in listOf("ListView", "list-view", "TreeView", "tree-view", "ComboBox", "combo-box",
				"TabItem", "tab", "TabView", "tabs", "TableView", "table", "TableColumn", "TableRow",
				"Menu", "menu-item", "Dialog", "dialog", "CollectionItem")) {
				assertTrue("Missing $name in $file completion", name in names)
			}
		}
	}

	fun testCompositeAttributesIncludeSelectionPopupAndOwnEvents() {
		val expected = mapOf(
			"ListView" to listOf("bind:selected-key", "bind:scroll-y", "on:selection-changed", "on:item-activated"),
			"TreeItem" to listOf("bind:key", "bind:expanded", "on:expanded-changed"),
			"ComboBox" to listOf("bind:selected-key", "drop-down-height", "on:open-changed"),
			"TableView" to listOf("bind:selected-key", "auto-sort", "on:column-clicked", "on:sorted"),
			"MenuItem" to listOf("bind:command", "checkable", "on:invoked", "on:checked-changed"),
			"Dialog" to listOf("bind:modal", "dismiss-on-escape", "on:open-changed", "on:closed"),
		)
		for ((tag, attributes) in expected) {
			myFixture.configureByText("page.vswml", "<$tag <caret>/>")
			val names = myFixture.completeBasic().orEmpty().map { it.lookupString }.toSet()
			attributes.forEach { assertTrue("Missing $tag.$it completion", it in names) }
		}
		myFixture.configureByText("page.vswml", "<Dialog modal=\"<caret>\"/>")
		assertEquals(setOf("true", "false"), myFixture.completeBasic().orEmpty().map { it.lookupString }.toSet())
	}

	fun testCompositeDocumentationRecognizesInheritedPropertiesAndOwnEvents() {
		val file = myFixture.configureByText("page.vswml", "<Dialog modal=\"true\" on:closed=\"Closed\"/>")
		val provider = GuiDocumentation()
		for (name in listOf("Dialog", "modal", "on:closed")) {
			val target = provider.documentationTargets(file, file.text.indexOf(name) + 1).single()
			assertNotNull("Missing $name documentation", target.computeDocumentation())
		}
	}

	fun testCompositeStyleCompletionKeepsOnlyStyleEnabledProperties() {
		myFixture.configureByText("page.vswcss", ":root { <caret> }")
		val names = myFixture.completeBasic().orEmpty().map { it.lookupString }.toSet()
		for (name in listOf("column-width", "selection-color", "drop-down-height"))
			assertTrue("Missing $name style completion", name in names)
		for (name in listOf("selected-key", "auto-sort", "modal", "command"))
			assertFalse("Unexpected $name style completion", name in names)
	}

	fun testInternalWidgetDocumentationKeepsItsActualQualifiedName() {
		val name = "VS::Gui::Internal::SelectionView"
		val type = requireNotNull(GuiCatalog.find(name))
		assertEquals(name, type.qualifiedName)
		assertSame(type, GuiCatalog.find("SelectionView"))
		assertNull(GuiCatalog.find("VS::Gui::SelectionView"))
		val file = myFixture.configureByText("page.vswml", "<SelectionView/>")
		val target = GuiDocumentation().documentationTargets(file, 2).single()
		val result = requireNotNull(target.computeDocumentation())
		// Read the SDK's rendered result, including the localized widget description.
		val html = result.javaClass.getMethod("getHtml").invoke(result) as String
		assertTrue(html, html.contains(name))
		assertFalse(html, html.contains("VS::Gui::SelectionView"))
		myFixture.configureByText("page.vswml", "<ListView <caret>/>")
		val inherited = myFixture.completeBasic().orEmpty().map { it.lookupString }.toSet()
		assertTrue("bind:selected-key" in inherited)
		assertTrue("bind:scroll-y" in inherited)
	}

	fun testUsedAliasesDoNotOfferDuplicatePropertySpellings() {
		myFixture.configureByText("page.vswml", "<Slider width=\"32px\" <caret>/>")
		val items = myFixture.completeBasic().orEmpty().map { it.lookupString }.toSet()
		assertFalse("Width" in items)
		assertFalse("bind:Width" in items)
		assertFalse("bind:width" in items)
		assertTrue("Height" in items)
	}

	fun testStyleCompletionUsesStyleEnabledMarks() {
		myFixture.configureByText("page.vswcss", ":root { <caret> }")
		val items = myFixture.completeBasic().orEmpty().map { it.lookupString }.toSet()
		assertTrue("width" in items)
		assertTrue("columns" in items)
		assertFalse("hit-test" in items)
		assertFalse("enabled" in items)
	}

	fun testAttributeInsertionLeavesCaretInsideQuotes() {
		myFixture.configureByText("page.vswml", "<Widget Wid<caret>/>")
		myFixture.completeBasic()
		val lookup = myFixture.lookup
		if (lookup != null) {
			lookup.currentItem = myFixture.lookupElements!!.single { it.lookupString == "Width" }
			myFixture.finishLookup('\n')
		}
		myFixture.checkResult("<Widget Width=\"<caret>\"/>")
	}

	fun testSrcReferencesResolveFromMarkupDirectory() {
		val style = myFixture.addFileToProject("ui/styles/page.vswcss", ":root { gap: 4px; }")
		val markup = myFixture.addFileToProject("ui/page.vswml", "<Widget><style src=\"styles/page.vswcss\"/></Widget>")
		val offset = markup.text.indexOf("page.vswcss") + 2
		val reference = requireNotNull(markup.findReferenceAt(offset))
		assertEquals(style, reference.resolve())
		assertFalse((reference as FileReference).fileReferenceSet.couldBeConvertedTo(true))
	}

	fun testNativeAbsoluteSrcPathsResolveOutsideProjectRootsAndKeepRenameOffsets() {
		val directory = Files.createTempDirectory("veroscript-gui-reference-").toAbsolutePath().normalize()
		val path = directory.resolve("page.vswcss")
		var virtual: com.intellij.openapi.vfs.VirtualFile? = null
		try {
			Files.writeString(path, ":root { gap: 4px; }")
			val target = requireNotNull(LocalFileSystem.getInstance().refreshAndFindFileByNioFile(path))
			virtual = target
			val expected = requireNotNull(PsiManager.getInstance(project).findFile(target))
			for (spelling in listOf(path.toString(), path.toString().replace('\\', '/')).distinct()) {
				val markup = myFixture.configureByText("absolute.vswml", "<Widget><style src=\"$spelling\"/></Widget>")
				val reference = requireNotNull(markup.findReferenceAt(markup.text.indexOf("page.vswcss") + 2))
				assertEquals(expected, reference.resolve())
				assertFalse((reference as FileReference).fileReferenceSet.couldBeConvertedTo(false))
				WriteCommandAction.runWriteCommandAction(project) { reference.handleElementRename("other.vswcss") }
				myFixture.checkResult("<Widget><style src=\"${spelling.removeSuffix("page.vswcss")}other.vswcss\"/></Widget>")
				val renamed = requireNotNull(markup.findReferenceAt(markup.text.indexOf("other.vswcss") + 2))
				WriteCommandAction.runWriteCommandAction(project) { renamed.bindToElement(expected) }
				myFixture.checkResult("<Widget><style src=\"${target.path}\"/></Widget>")
			}
		}
		finally
		{
			virtual?.takeIf { it.isValid }?.let { file ->
				WriteCommandAction.runWriteCommandAction(project) { file.delete(this) }
			}
			Files.deleteIfExists(path)
			Files.deleteIfExists(directory)
		}
	}

	fun testWhitespacePaddedPathsDoNotResolveToDifferentTrimmedFiles() {
		myFixture.addFileToProject("ui/page.vswcss", ":root { gap: 4px; }")
		for ((index, padding) in listOf(" ", "\u2003").withIndex()) {
			val markup = myFixture.addFileToProject("ui/padded$index.vswml", "<Widget><style src=\"${padding}page.vswcss$padding\"/></Widget>")
			assertNull(markup.findReferenceAt(markup.text.indexOf("page.vswcss") + 2))
		}
		for ((index, padding) in listOf("\u00a0", "\u202f").withIndex()) {
			val name = "${padding}page.vswcss$padding"
			val style = myFixture.addFileToProject("ui/$name", ":root { gap: 4px; }")
			val markup = myFixture.addFileToProject("ui/nonbreaking$index.vswml", "<Widget><style src=\"$name\"/></Widget>")
			assertEquals(style, markup.findReferenceAt(markup.text.indexOf("page.vswcss") + 2)?.resolve())
		}
	}

	fun testNativeFilenameCharactersDoNotResolveToDifferentFiles() {
		val nested = myFixture.addFileToProject("ui/styles/page.vswcss", ":root { gap: 4px; }")
		val queryDecoy = myFixture.addFileToProject("ui/page", "")
		val markup = myFixture.addFileToProject("ui/native.vswml",
			"<Widget><style src=\"styles\\page.vswcss\"/><style src=\"page?.vswcss\"/></Widget>")
		var backslashTarget: com.intellij.psi.PsiFile? = if (java.io.File.separatorChar == '\\') nested else null
		var queryTarget: com.intellij.psi.PsiFile? = null
		if (java.io.File.separatorChar == '/') {
			// Some VFS implementations reject native POSIX filename characters.
			WriteCommandAction.runWriteCommandAction(project) {
				val directory = markup.virtualFile.parent
				val manager = PsiManager.getInstance(project)
				if (directory.fileSystem.isValidName("styles\\page.vswcss")) {
					backslashTarget = requireNotNull(manager.findFile(directory.createChildData(this, "styles\\page.vswcss")))
				}
				if (directory.fileSystem.isValidName("page?.vswcss")) {
					queryTarget = requireNotNull(manager.findFile(directory.createChildData(this, "page?.vswcss")))
				}
			}
		}
		val backslash = requireNotNull(markup.findReferenceAt(markup.text.indexOf("page.vswcss") + 2))
		val query = requireNotNull(markup.findReferenceAt(markup.text.indexOf("page?.vswcss") + 2))
		assertEquals(backslashTarget, backslash.resolve())
		assertEquals(queryTarget, query.resolve())
		assertNotSame(queryDecoy, query.resolve())
	}

	fun testUnknownWidgetsHaveNoFabricatedParserErrors() {
		myFixture.configureByText("custom.vswml", "<ApplicationControl bind:custom=\"ReadValue\" on:custom=\"Handle\"/>")
		myFixture.checkHighlighting(true, false, false)
	}
}

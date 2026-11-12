// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

package com.vanstysanic.veroscript.gui.jetbrains

import com.intellij.lang.LanguageParserDefinitions
import com.intellij.openapi.fileTypes.FileTypeManager
import com.intellij.testFramework.TestApplicationManager
import com.vanstysanic.veroscript.gui.jetbrains.language.*
import org.junit.Assert.*
import org.junit.BeforeClass
import org.junit.Test
import java.util.Locale
import java.util.Properties

class GuiRegistrationTest {
	@Test fun closingTagsPreserveExistingSourceAndDoNotEnterLiterals() {
		assertEquals("</Widget>", GuiTagClosing.suffix("<Widget>", 8))
		val existing = "<Widget><Text/></Widget>"
		assertNull(GuiTagClosing.suffix(existing, 8))
		assertEquals("</Widget>", GuiTagClosing.suffix("<Widget><Widget></Widget>", 8))
		assertNull(GuiTagClosing.suffix("<Widget><Widget></Widget></Widget>", 8))
		assertNull(GuiTagClosing.suffix("<Widget><Widget/></Widget>", 8))
		assertNull(GuiTagClosing.suffix("<Widget/>", 9))
		assertNull(GuiTagClosing.suffix("<Text Text=\"unfinished>", 23))
		assertNull(GuiTagClosing.suffix("<!-- <Widget> -->", 13))
		assertNull(GuiTagClosing.suffix("<script><![CDATA[print(\">\");]]></script>", 26))
	}

	@Test fun guiFileTypesKeepVsWithBasePluginAndDoNotClaimBakedArtifacts() {
		val types = FileTypeManager.getInstance()
		assertSame(GuiMarkupFileType, types.getFileTypeByFileName("page.vswml"))
		assertSame(GuiStyleFileType, types.getFileTypeByFileName("page.vswcss"))
		assertSame(com.vanstysanic.veroscript.jetbrains.language.VeroScriptFileType, types.getFileTypeByFileName("logic.vs"))
		assertNotSame(GuiMarkupFileType, types.getFileTypeByFileName("app.vsapp"))
		assertNotNull(LanguageParserDefinitions.INSTANCE.forLanguage(GuiMarkupLanguage))
		assertNotNull(LanguageParserDefinitions.INSTANCE.forLanguage(GuiStyleLanguage))
	}

	@Test fun incompleteNamesAndNamespacesDoNotCreateXmlNamespaceErrors() {
		val model = GuiMarkupModel.parse("<Widget bind:width=\"{ Count }\"><Text/></Widget><")
		assertEquals(listOf("Widget", "Text", "Widget", ""), model.tags.map { it.name })
		assertEquals("bind:width", model.tags.first().attributes.single().name)
		assertTrue(model.openTags(model.tags.last().range.startOffset).isEmpty())
		val nested = GuiMarkupModel.parse("<Widget><Text></")
		assertEquals(listOf("Widget", "Text"), nested.openTags(nested.tags.last().range.startOffset).map { it.name })
	}

	@Test fun allSixBundlesHaveMatchingKeysAndWorkingMessageArguments() {
		val locales = listOf("", "_zh_CN", "_zh_TW", "_ja_JP", "_ko_KR", "_fr_FR")
		val bundles = locales.map { suffix -> Properties().apply {
			requireNotNull(this@GuiRegistrationTest.javaClass.getResourceAsStream("/Localization/messages/VeroScriptGuiBundle$suffix.properties"))
				.bufferedReader(Charsets.UTF_8).use { load(it) }
		} }
		bundles.forEach { bundle ->
			assertEquals(bundles.first().stringPropertyNames(), bundle.stringPropertyNames())
			for (key in bundle.stringPropertyNames()) {
				val pattern = bundle.getProperty(key)
				assertTrue(pattern.isNotBlank())
				val indexes = Regex("\\{(\\d+)\\}").findAll(pattern).map { it.groupValues[1].toInt() }.toSet()
				if (indexes.isNotEmpty()) {
					val args = Array(indexes.max() + 1) { "gui-value-$it" }
					val output = java.text.MessageFormat(pattern, Locale.ROOT).format(args)
					indexes.forEach { assertTrue(output.contains(args[it])) }
				}
			}
		}
	}

	companion object {
		@JvmStatic @BeforeClass fun initializeApplication() { TestApplicationManager.getInstance() }
	}
}

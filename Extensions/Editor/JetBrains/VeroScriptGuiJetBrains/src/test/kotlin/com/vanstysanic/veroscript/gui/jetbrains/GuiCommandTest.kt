// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

package com.vanstysanic.veroscript.gui.jetbrains

import com.google.gson.JsonObject
import com.intellij.testFramework.TestApplicationManager
import com.vanstysanic.veroscript.gui.jetbrains.run.GuiCommand
import com.vanstysanic.veroscript.gui.jetbrains.run.GuiRunOptions
import com.vanstysanic.veroscript.jetbrains.runtime.RuntimeLayout
import org.junit.Assert.*
import org.junit.BeforeClass
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder
import java.nio.file.Files
import java.nio.file.Path

class GuiCommandTest {
	@get:Rule val temporary = TemporaryFolder()
	private fun resolve(options: GuiRunOptions, semantics: JsonObject = JsonObject()): GuiCommand {
		val root = temporary.root.toPath()
		return GuiCommand.resolve(options, root, root, RuntimeLayout(root.resolve("runtime")), semantics, "en-US")
	}
	private fun options(extension: String = "vswml") = GuiRunOptions().apply {
		val source = temporary.newFile("entry 空格.$extension").toPath()
		entry = GuiCommand.persist(source, temporary.root.toPath())
	}

	@Test fun taskSelectionRetainsSharedTemplateSettingsAndClearsIncompatibleArguments() {
		val options = options().apply {
			arguments = "preview argument"
			frames = "3"
			environment = "{\"GUI_THEME\":\"dark\"}"
			guiBackend = "rmlui"
			semantics = "{\"defines\":[\"GUI_TEST=1\"]}"
		}
		options.selectTask("check")
		assertEquals("check", resolve(options).task)
		assertTrue(options.arguments.isNullOrEmpty())
		assertTrue(options.frames.isNullOrEmpty())
		assertEquals("rmlui", options.guiBackend)
		assertTrue(options.semantics.orEmpty().contains("GUI_TEST=1"))
		assertEquals("dark", resolve(options).environment["GUI_THEME"])
		options.selectTask("compile")
		options.output = "artifacts/app.vsapp"
		options.applicationName = "Application"
		options.assets = "[]"
		options.selectTask("compile")
		assertEquals("artifacts/app.vsapp", options.output)
		assertEquals("Application", options.applicationName)
		options.selectTask("preview")
		assertEquals("preview", resolve(options).task)
		assertTrue(options.output.isNullOrEmpty())
		assertTrue(options.applicationName.isNullOrEmpty())
		assertEquals("[]", options.assets)
		options.entry = temporary.newFile("compiled.vsapp").path
		options.frames = "3"
		options.arguments = "artifact argument"
		options.selectTask("preview")
		assertNull(resolve(options).frames)
		assertEquals(listOf("artifact", "argument"), resolve(options).scriptArguments)
	}

	@Test fun previewKeepsModuleOptionsBeforeTaskAndScriptArgumentsAfterSeparator() {
		val options = options().apply {
			platformBackend = "sdl3"
			graphicsBackend = "sdl3"
			guiBackend = "rmlui"
			packageRoot = "."
			frames = "9223372036854775807"
			arguments = "\"two words\" --output ignored"
		}
		val semantics = JsonObject().apply { addProperty("stdlibPath", temporary.root.resolve("stdlib").path) }
		val command = resolve(options, semantics)
		val args = command.arguments()
		assertTrue(args.indexOf("--stdlib-path") < args.indexOf("--platform-backend"))
		assertTrue(args.indexOf("--path") < args.indexOf("--platform-backend"))
		assertTrue(args.indexOf("--gui-backend") < args.indexOf("gui-preview"))
		assertEquals(Long.MAX_VALUE, command.frames)
		assertEquals(listOf("two words", "--output", "ignored"), args.drop(args.indexOf("--") + 1))
		assertTrue(command.entry.isAbsolute)
	}

	@Test fun checkingAndBakingSelectVmBeforeThePackageEnvironment() {
		for (extension in listOf("vswml", "vs")) {
			val options = options(extension).apply { packageRoot = "." }
			for (task in listOf("check", "compile")) {
				options.selectTask(task)
				if (task == "compile") options.output = "artifacts/app.vsapp"
				val arguments = resolve(options).arguments()
				assertEquals(listOf("--backend=vm"), arguments.filter { it.startsWith("--backend=") })
				assertTrue(arguments.indexOf("--backend=vm") < arguments.indexOf("--path"))
				assertTrue(arguments.indexOf("--path") < arguments.indexOf("gui-" + task))
				assertFalse(arguments.any { it.startsWith("--jit-backend") })
			}
			options.selectTask("preview")
			assertFalse(resolve(options).arguments().any { it.startsWith("--backend=") || it.startsWith("--jit-backend") })
		}
	}

	@Test fun backendSelectionsPreserveOmissionAndExplicitValuesAfterPersistence() {
		val options = options().apply { packageRoot = "." }
		val roles = listOf("platform", "graphics", "gui", "image", "font", "text-layout")
		for (value in listOf("", "auto", "none", "custom-provider")) {
			options.platformBackend = value
			options.graphicsBackend = value
			options.guiBackend = value
			options.imageBackend = value
			options.fontBackend = value
			options.textLayoutBackend = value
			val copy = com.intellij.util.xmlb.XmlSerializer.deserialize(
				com.intellij.util.xmlb.XmlSerializer.serialize(options), GuiRunOptions::class.java)
			val command = resolve(copy)
			assertEquals(temporary.root.toPath().toAbsolutePath().normalize(), command.packageRoot)
			val arguments = command.arguments()
			for (role in roles) {
				val flag = "--" + role + "-backend"
				if (value.isEmpty()) {
					assertFalse(arguments.toString(), flag in arguments)
					continue
				}
				assertEquals(arguments.toString(), 1, arguments.count { it == flag })
				val index = arguments.indexOf(flag)
				assertEquals(value, arguments[index + 1])
				assertTrue(arguments.toString(), index < arguments.indexOf("gui-preview"))
			}
			assertFalse("--backend" in arguments || "--vm-backend" in arguments || "--jit-backend" in arguments)
		}
	}

	@Test fun bakeSupportsRepeatedAssetsAndRejectsOverwritingAnyInput() {
		val options = options().apply {
			task = "compile"
			output = "dist/app.vsapp"
			applicationName = "Demo 中文"
			assets = "[\"image one.png\",\"font.ttf\"]"
		}
		temporary.newFile("image one.png")
		temporary.newFile("font.ttf")
		val args = resolve(options).arguments()
		assertEquals(2, args.count { it == "--asset" })
		assertEquals("Demo 中文", args[args.indexOf("--name") + 1])
		assertFalse("--" in args)
		val asset = temporary.newFile("asset.vsapp")
		options.output = asset.path
		options.assets = "[\"asset.vsapp\"]"
		assertThrows(IllegalArgumentException::class.java) { resolve(options) }
	}

	@Test fun nativeEntryAndFrameRestrictionsAreNotSilentlyIgnored() {
		for (extension in listOf("vs", "vsapp")) {
			val options = options(extension).apply { frames = "1" }
			assertThrows(IllegalArgumentException::class.java) { resolve(options) }
		}
		val options = options()
		for (value in listOf("-1", "9223372036854775808", "1.5", "invalid")) {
			options.frames = value
			assertThrows(IllegalArgumentException::class.java) { resolve(options) }
		}
		options.frames = "0"
		assertFalse("--frames" in resolve(options).arguments())
		options.task = "check"
		options.arguments = "--script-option"
		assertThrows(IllegalArgumentException::class.java) { resolve(options) }
		options.arguments = ""
		options.output = "unused.vsapp"
		assertThrows(IllegalArgumentException::class.java) { resolve(options) }
	}

	@Test fun portablePathsKeepEntryOriginWhenWorkingDirectoryChanges() {
		val original = Path.of("original project").toAbsolutePath()
		val moved = temporary.root.toPath()
		val source = moved.resolve("src/page.vswml")
		Files.createDirectories(source.parent)
		Files.writeString(source, "<Widget/>")
		val cwd = Files.createDirectory(moved.resolve("work"))
		val options = GuiRunOptions().apply { entry = GuiCommand.persist(original.resolve("src/page.vswml"), original) }
		val result = GuiCommand.resolve(options, moved, cwd, RuntimeLayout(moved), JsonObject(), "en-US")
		assertEquals(source.toAbsolutePath().normalize(), result.entry)
		options.entry = "../src/page.vswml"
		assertEquals(result.entry, GuiCommand.resolve(options, moved, cwd, RuntimeLayout(moved), JsonObject(), "en-US").entry)
	}

	@Test fun persistedEntriesPreserveNativeFilenameCharacters() {
		val root = temporary.root.toPath()
		val entry = root.resolve("designs\\draft.vswml")
		Files.createDirectories(entry.parent)
		Files.writeString(entry, "<Widget/>")
		val stored = GuiCommand.persist(entry, root)
		val relative = if (java.io.File.separatorChar == '\\') "designs/draft.vswml"
		else "designs\\draft.vswml"
		assertEquals("\$PROJECT_DIR\$/" + relative, stored)
		val options = GuiRunOptions().apply { this.entry = stored }
		assertEquals(entry.toAbsolutePath().normalize(), resolve(options).entry)
	}

	@Test fun environmentAndOptionsRoundTripPreserveExplicitSettings() {
		val options = options().apply {
			environment = "{\"GUI_EXAMPLE\":\"value 空格\"}"
			inheritParentEnvironment = false
			semantics = "{\"defines\":[\"GUI_TEST=1\"]}"
			fontBackend = "font-backend"
			textLayoutBackend = "test-layout"
		}
		val copy = com.intellij.util.xmlb.XmlSerializer.deserialize(com.intellij.util.xmlb.XmlSerializer.serialize(options), GuiRunOptions::class.java)
		assertEquals(options.entry, copy.entry)
		assertEquals(options.semantics, copy.semantics)
		assertEquals("font-backend", copy.fontBackend)
		assertEquals("test-layout", copy.textLayoutBackend)
		val arguments = resolve(copy).arguments()
		assertEquals("test-layout", arguments[arguments.indexOf("--text-layout-backend") + 1])
		assertFalse(copy.inheritParentEnvironment)
		assertEquals("value 空格", resolve(copy).environment["GUI_EXAMPLE"])
		options.environment = "{\"INVALID=NAME\":\"x\"}"
		assertThrows(IllegalArgumentException::class.java) { resolve(options) }
	}

	companion object {
		@JvmStatic @BeforeClass fun initializeApplication() { TestApplicationManager.getInstance() }
	}
}

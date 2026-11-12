// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

package com.vanstysanic.veroscript.jetbrains

import com.vanstysanic.veroscript.jetbrains.run.ResolvedRun
import com.vanstysanic.veroscript.jetbrains.run.RunProgramPaths
import com.vanstysanic.veroscript.jetbrains.run.VeroScriptRunOptions
import com.vanstysanic.veroscript.jetbrains.runtime.RuntimeLayout
import com.vanstysanic.veroscript.jetbrains.settings.SemanticOptions
import org.junit.Assert.*
import org.junit.Test
import java.nio.file.Files
import java.nio.file.Path
import java.nio.file.attribute.PosixFilePermissions

class ConfigurationTest {
	@Test fun checkForcesVmBeforePackageSelectionWithoutChangingTheRunPreference() {
		val root = Files.createTempDirectory("vero check backend ")
		val windows = System.getProperty("os.name").startsWith("Windows", true)
		val cli = root.resolve(if (windows) "veroscript.exe"
		else "veroscript")
		val program = root.resolve("main.vs")
		try {
			Files.writeString(cli, "fixture only; never executed")
			if (!windows) Files.setPosixFilePermissions(cli, PosixFilePermissions.fromString("rwx------"))
			Files.writeString(program, "void main() {}")
			val template = ResolvedRun(RuntimeLayout(root), program, root, emptyList(), emptyMap(),
				SemanticOptions.parse("{}"), "run", "jit", null, null, "localhost", 4711, 0, "en-US",
				jitBackendPath = root.resolve("missing-backend"))
			for (backend in listOf("vm", "mixed", "jit")) {
				val run = template.copy(backend = backend)
				val check = run.copy(mode = "check").commandLine().parametersList.list
				assertTrue(check.contains("--check"))
				assertEquals(listOf("--backend=vm"), check.filter { it.startsWith("--backend=") })
				assertFalse(check.any { it.startsWith("--jit-backend") })
				assertTrue(check.indexOf("--backend=vm") < check.indexOf(program.toString()))
				assertEquals(backend, run.backend)
				assertEquals(listOf("--backend=$backend"), run.copy(jitBackendPath = null).commandLine()
					.parametersList.list.filter { it.startsWith("--backend=") })
			}
			assertThrows(IllegalArgumentException::class.java) { template.commandLine() }
		}
		finally
		{
			Files.deleteIfExists(program)
			Files.deleteIfExists(cli)
			Files.deleteIfExists(root)
		}
	}

	@Test fun contextModesDiscardIncompatibleTemplateOptionsAndKeepLaunchSettings() {
		val options = VeroScriptRunOptions().apply {
			mode = "attach"
			attachMode = "domain"
			domainId = "host-domain"
			overrideArguments = true
			arguments = "\"two words\""
			workingDirectory = "work"
			environment = "{\"APP_MODE\":\"gui\"}"
			semantics = "{\"defines\":[\"PROJECT_FEATURE=1\"]}"
			backend = "jit"
			jitBackendPath = "backend/module"
			inputFile = "input.txt"
			stopOnEntry = "true"
		}
		options.selectContextMode("run")
		assertEquals("run", options.mode)
		assertEquals("program", options.attachMode)
		// SDK string properties can normalize an empty value to null.
		assertTrue(options.domainId.isNullOrEmpty())
		assertFalse(options.overrideArguments)
		assertEquals("\"two words\"", options.arguments)
		assertEquals("backend/module", options.jitBackendPath)
		assertEquals("input.txt", options.inputFile)
		options.selectContextMode("check")
		assertEquals("check", options.mode)
		assertTrue(options.arguments.isNullOrEmpty())
		assertTrue(options.jitBackendPath.isNullOrEmpty())
		assertTrue(options.inputFile.isNullOrEmpty())
		assertEquals("work", options.workingDirectory)
		assertEquals("{\"APP_MODE\":\"gui\"}", options.environment)
		assertEquals("{\"defines\":[\"PROJECT_FEATURE=1\"]}", options.semantics)
		assertEquals("jit", options.backend)
		assertEquals("true", options.stopOnEntry)
	}

	@Test fun contextEntriesSurviveProjectMovesAndWorkingDirectoryOverrides() {
		val original = Path.of("original project").toAbsolutePath()
		val moved = Path.of("moved project 中文").toAbsolutePath()
		val entry = original.resolve("src/main.vs")
		val options = VeroScriptRunOptions().apply {
			program = RunProgramPaths.persist(entry, original)
			workingDirectory = "work"
		}
		assertEquals("\$PROJECT_DIR\$/src/main.vs", options.program)
		assertEquals(moved.resolve("src/main.vs"), RunProgramPaths.resolve(options, moved))
		options.program = "../src/main.vs"
		assertEquals(moved.resolve("src/main.vs"), RunProgramPaths.resolve(options, moved))
	}

	@Test fun externalContextEntriesStayAbsoluteAndBlankEntriesStayAbsent() {
		val root = Path.of("project").toAbsolutePath()
		val external = root.resolveSibling("external/main.vs")
		assertEquals(external.toString(), RunProgramPaths.persist(external, root))
		assertNull(RunProgramPaths.resolve(VeroScriptRunOptions(), root))
	}

	@Test fun runConfigurationOptionsRoundTripWithoutExpandingPersonalPaths() {
		val options = com.vanstysanic.veroscript.jetbrains.run.VeroScriptRunOptions().apply {
			program = "\$PROJECT_DIR\$/src/main.vs"
			arguments = "\"\" \"two words\""
			overrideArguments = true
			stopOnEntry = "false"
			semantics = """{"defines":[]}"""
			mode = "attach"
			inheritParentEnvironment = false
			launchTimeoutSeconds = 120
			jitBackendPath = "${'$'}PROJECT_DIR${'$'}/jit/backend"
		}
		val xml = com.intellij.util.xmlb.XmlSerializer.serialize(options)
		val copy = com.intellij.util.xmlb.XmlSerializer.deserialize(xml, options.javaClass)
		assertEquals(options.program, copy.program)
		assertEquals(options.arguments, copy.arguments)
		assertTrue(copy.overrideArguments)
		assertEquals("false", copy.stopOnEntry)
		assertEquals(options.semantics, copy.semantics)
		assertEquals("attach", copy.mode)
		assertFalse(copy.inheritParentEnvironment)
		assertEquals(120, copy.launchTimeoutSeconds)
		assertEquals(options.jitBackendPath, copy.jitBackendPath)
	}

	@Test fun inheritedPathsKeepTheirOriginalBase() {
		val project = Path.of("project with spaces").toAbsolutePath()
		val working = project.resolve("work")
		val inherited = SemanticOptions.resolve(SemanticOptions.parse("""{"importPaths":["src","库"],"defines":["A=1"]}"""), project)
		val override = SemanticOptions.resolve(SemanticOptions.parse("""{"bindingCatalogPaths":["bindings.json"],"defines":[]}"""), working)
		override.entrySet().forEach { (key, value) -> inherited.add(key, value) }
		assertEquals(project.resolve("src").toString(), inherited.getAsJsonArray("importPaths")[0].asString)
		assertEquals(working.resolve("bindings.json").toString(), inherited.getAsJsonArray("bindingCatalogPaths")[0].asString)
		assertEquals(0, inherited.getAsJsonArray("defines").size())
	}

	@Test fun attachDoesNotOverrideHostProcessSettings() {
		val root = Path.of(".").toAbsolutePath()
		val run = ResolvedRun(null, null, root, emptyList(), emptyMap(), SemanticOptions.parse("{}"),
			"attach", "mixed", null, null, "localhost", 4711, 1024, "en-US")
		val args = run.debugArguments()
		assertTrue(args.isEmpty())
		for (key in listOf("cwd", "workspace", "env", "backend", "maxCallDepth", "stopOnEntry")) assertFalse(args.containsKey(key))
		assertEquals(false, run.copy(stopOnEntry = false).debugArguments()["stopOnEntry"])
		assertEquals(emptyList<String>(), run.copy(overrideArguments = true).debugArguments()["args"])
	}

	@Test fun macroCliEncodingPreservesEmptyAndVariadicParametersAndBody() {
		fun macro(value: String) = ResolvedRun.macroArgument(SemanticOptions.parse("""{"predefinedMacros":[$value]}""")
			.getAsJsonArray("predefinedMacros")[0].asJsonObject)
		assertEquals("F()= a=b ", macro("""{"name":"F","body":" a=b "}"""))
		assertEquals("F(a,...)=a", macro("""{"name":"F","parameters":["a"],"variadic":true,"body":"a"}"""))
		assertEquals("F(...)=__VA_ARGS__", macro("""{"name":"F","variadic":true,"body":"__VA_ARGS__"}"""))
	}

	@Test fun invalidShapesAndLimitsFailBeforeStartingTools() {
		for (value in listOf("[]", """{"importPaths":"src"}""", """{"importPaths":[""]}""",
			"""{"allowCircularImports":"true"}""", """{"lspAnalysisTimeoutMs":49}""",
			"""{"lspAnalysisTimeoutMs":50.5}""", """{"unknown":true}""",
			"""{"predefinedMacros":[{"name":"F","parameters":["a","a"],"body":"a"}]}""")) {
			assertThrows(IllegalArgumentException::class.java) { SemanticOptions.parse(value) }
		}
	}

	@Test fun projectMacroInWorkingDirectoryDoesNotDuplicateBase() {
		val project = Path.of("project").toAbsolutePath()
		assertEquals(project.resolve("src"), SemanticOptions.absolute("\$PROJECT_DIR\$/src", project.resolve("work"), project))
		val options = SemanticOptions.resolve(SemanticOptions.parse("""{"importPaths":["${'$'}PROJECT_DIR${'$'}/src"]}"""), project.resolve("work"), project)
		assertEquals(project.resolve("src").toString(), options.getAsJsonArray("importPaths")[0].asString)
	}
}

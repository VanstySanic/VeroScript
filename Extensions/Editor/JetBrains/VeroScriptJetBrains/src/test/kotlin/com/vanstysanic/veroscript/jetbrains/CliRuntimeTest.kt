// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

package com.vanstysanic.veroscript.jetbrains

import com.intellij.util.execution.ParametersListUtil
import com.vanstysanic.veroscript.jetbrains.run.ResolvedRun
import com.vanstysanic.veroscript.jetbrains.runtime.RuntimeLayout
import com.vanstysanic.veroscript.jetbrains.settings.SemanticOptions
import org.junit.Assert.*
import org.junit.Assume.assumeTrue
import org.junit.Test
import java.nio.file.Files
import java.nio.file.Path
import java.util.concurrent.TimeUnit

class CliRuntimeTest {
	@Test fun exportedAliasesWorkThroughSourceAndLibraryFacades() {
		val home = System.getenv("VEROSCRIPT_TEST_RUNTIME")
		assumeTrue("Set VEROSCRIPT_TEST_RUNTIME to run native protocol integration", !home.isNullOrBlank())
		val runtime = RuntimeLayout(Path.of(home))
		val directory = Files.createTempDirectory("vero aliases 空格 ")
		val aliases = directory.resolve("aliases.vs")
		val library = directory.resolve("aliases.vslib")
		val facade = directory.resolve("facade.vs")
		val program = directory.resolve("main.vs")
		try {
			Files.writeString(aliases, "namespace Measures { export using Count = int64; " +
				"export typedef Label = string; using Hidden = int; }\n")
			Files.writeString(program, "import \"facade.vs\"; int main(){ Measures::Count count = 2147483648i64; " +
				"Measures::Label label = \"aliases\"; print(count); print(label); return 0; }\n")
			val build = ProcessBuilder(runtime.tool("").toString(), "--emit-package", library.toString(), aliases.toString())
				.directory(directory.toFile()).redirectErrorStream(true).start()
			try {
				assertTrue(build.waitFor(10, TimeUnit.SECONDS))
				assertEquals(build.inputStream.bufferedReader(Charsets.UTF_8).readText(), 0, build.exitValue())
			}
			finally
			{
				close(build)
			}
			for (moduleName in listOf("aliases.vs", "aliases.vslib")) {
				Files.writeString(facade, "export import \"$moduleName\";\n")
				val run = ResolvedRun(runtime, program, directory, emptyList(), emptyMap(), SemanticOptions.parse("{}"),
					"run", "vm", null, null, "localhost", 4711, 0, "en-US")
				for (mode in listOf("check", "run")) {
					val process = run.copy(mode = mode).commandLine().withRedirectErrorStream(true).createProcess()
					try {
						assertTrue(process.waitFor(10, TimeUnit.SECONDS))
						val output = process.inputStream.bufferedReader(Charsets.UTF_8).readText().replace("\r\n", "\n")
						assertEquals("$moduleName / $mode: $output", 0, process.exitValue())
						if (mode == "run") assertEquals("2147483648\naliases\n", output)
					}
					finally
					{
						close(process)
					}
				}
			}
		}
		finally
		{
			for (path in listOf(program, facade, library, aliases)) Files.deleteIfExists(path)
			Files.deleteIfExists(directory)
		}
	}

	@Test fun mixedUsesVmAndForcedJitWithoutModuleDoesNotExecuteEffects() {
		val home = System.getenv("VEROSCRIPT_TEST_RUNTIME")
		assumeTrue("Set VEROSCRIPT_TEST_RUNTIME to run native protocol integration", !home.isNullOrBlank())
		val directory = Files.createTempDirectory("vero backend 空格 ")
		val program = directory.resolve("backend.vs")
		Files.writeString(program, "int main() { print(99); return 0; }")
		val run = ResolvedRun(RuntimeLayout(Path.of(home)), program, directory, emptyList(), emptyMap(),
			SemanticOptions.parse("{}"), "run", "mixed", null, null, "localhost", 4711, 0, "en-US")
		try {
			for (backend in listOf("mixed", "jit")) {
				val process = run.copy(backend = backend).commandLine().createProcess()
				try {
					assertTrue(process.waitFor(10, TimeUnit.SECONDS))
					val output = process.inputStream.bufferedReader().readText().replace("\r\n", "\n")
					val error = process.errorStream.bufferedReader().readText()
					if (backend == "mixed") {
						assertEquals(error, 0, process.exitValue())
						assertEquals("99\n", output)
					}
					else
					{
						assertNotEquals(error, 0, process.exitValue())
						assertEquals("", output)
						assertTrue(error, error.contains("explicitly selected backend"))
					}
				}
				finally
				{
					close(process)
				}
			}
		}
		finally
		{
			deleteProgram(directory, program)
		}
	}

	@Test fun checkUsesVmWithoutAJitModuleAndDoesNotExecuteScriptEffects() {
		val home = System.getenv("VEROSCRIPT_TEST_RUNTIME")
		assumeTrue("Set VEROSCRIPT_TEST_RUNTIME to run native compile-only integration", !home.isNullOrBlank())
		val directory = Files.createTempDirectory("vero check backend 空格 ")
		val program = directory.resolve("backend.vs")
		try {
			Files.writeString(program, "int main() { print(99); return 0; }")
			val run = ResolvedRun(RuntimeLayout(Path.of(home)), program, directory, emptyList(), emptyMap(),
				SemanticOptions.parse("{}"), "check", "jit", null, null, "localhost", 4711, 0, "en-US",
				jitBackendPath = directory.resolve("missing-backend"))
			val command = run.commandLine()
			assertTrue(command.parametersList.list.contains("--backend=vm"))
			assertFalse(command.parametersList.list.any { it.startsWith("--jit-backend") })
			assertEquals("jit", run.backend)
			val process = command.withRedirectErrorStream(true).createProcess()
			try {
				assertTrue(process.waitFor(10, TimeUnit.SECONDS))
				val output = process.inputStream.bufferedReader(Charsets.UTF_8).readText()
				assertEquals(output, 0, process.exitValue())
				assertEquals("", output)
			}
			finally
			{
				close(process)
			}
		}
		finally
		{
			deleteProgram(directory, program)
		}
	}

	@Test fun explicitJitModuleSupportsFileAndDirectoryWithoutLeakingIntoDebugOrCheck() {
		val home = System.getenv("VEROSCRIPT_TEST_RUNTIME")
		val module = System.getenv("VEROSCRIPT_TEST_JIT_BACKEND")
		assumeTrue("Set VEROSCRIPT_TEST_RUNTIME and VEROSCRIPT_TEST_JIT_BACKEND for native JIT integration",
			!home.isNullOrBlank() && !module.isNullOrBlank())
		val directory = Files.createTempDirectory("vero jit 空格 ")
		val program = directory.resolve("backend.vs")
		Files.writeString(program, "int value() { return 42; } int main() { print(value()); return 0; }")
		val run = ResolvedRun(RuntimeLayout(Path.of(home)), program, directory, emptyList(), emptyMap(),
			SemanticOptions.parse("{}"), "run", "jit", null, null, "localhost", 4711, 0, "en-US")
		try {
			for (backend in listOf(Path.of(module), Path.of(module).parent)) {
				val selected = run.copy(jitBackendPath = backend)
				val command = selected.commandLine()
				val argv = command.parametersList.list
				assertEquals(1, argv.count { it == "--jit-backend=$backend" })
				assertTrue(argv.indexOf("--jit-backend=$backend") < argv.indexOf(program.toString()))
				assertFalse(selected.debugArguments().keys.any { it.contains("jit", true) })
				val process = command.createProcess()
				try {
					assertTrue(process.waitFor(10, TimeUnit.SECONDS))
					val output = process.inputStream.bufferedReader().readText().replace("\r\n", "\n")
					assertEquals(process.errorStream.bufferedReader().readText(), 0, process.exitValue())
					assertEquals("42\n", output)
				}
				finally
				{
					close(process)
				}
			}
			val missing = run.copy(jitBackendPath = directory.resolve("missing"))
			assertThrows(IllegalArgumentException::class.java) { missing.commandLine() }
			val check = missing.copy(mode = "check").commandLine()
			assertFalse(check.parametersList.list.any { it.startsWith("--jit-backend") })
			assertTrue(check.parametersList.list.contains("--backend=vm"))
		}
		finally
		{
			deleteProgram(directory, program)
		}
	}

	private fun close(process: Process) {
		process.destroyForcibly().waitFor(5, TimeUnit.SECONDS)
		process.inputStream.close()
		process.errorStream.close()
		process.outputStream.close()
	}

	private fun deleteProgram(directory: Path, program: Path) {
		Files.deleteIfExists(directory.resolve(".verocache/backend.vs.vsbc"))
		Files.deleteIfExists(directory.resolve(".verocache"))
		Files.deleteIfExists(program)
		Files.deleteIfExists(directory)
	}

	@Test fun runAndCheckPreserveArgumentsAndMacroOptions() {
		val home = System.getenv("VEROSCRIPT_TEST_RUNTIME")
		assumeTrue("Set VEROSCRIPT_TEST_RUNTIME to run native protocol integration", !home.isNullOrBlank())
		val directory = Files.createTempDirectory("vero cli 空格 ")
		val program = directory.resolve("arguments.vs")
		Files.writeString(program, "int main(string[] args) { print(F()); print(\"[PARENT:\" + std::environment::getenv(\"VEROSCRIPT_TEST_RUNTIME\") + \"]\"); print(\"[EXPLICIT:\" + std::environment::getenv(\"VERO_TEST_EXPLICIT\") + \"]\"); print(len(args)); for (int i = 0; i < len(args); i = i + 1) { print(\"[\" + args[i] + \"]\"); } return 0; }")
		val args = ParametersListUtil.parse("\"\" \"two words\" 中文 --check")
		assertEquals(listOf("", "two words", "中文", "--check"), args)
		val run = ResolvedRun(RuntimeLayout(Path.of(home)), program, directory, args, mapOf("VERO_TEST_EXPLICIT" to "two words中文"),
			SemanticOptions.parse("""{"predefinedMacros":[{"name":"F","body":"7"}]}"""), "run", "vm", null,
			null, "localhost", 4711, 0, "en-US", inheritParentEnvironment = false)
		assertEquals(com.intellij.execution.configurations.GeneralCommandLine.ParentEnvironmentType.NONE, run.commandLine().parentEnvironmentType)
		val processes = mutableListOf<Process>()
		try {
			val process = run.commandLine().createProcess().also { processes.add(it) }
			assertTrue(process.waitFor(10, TimeUnit.SECONDS))
			val output = process.inputStream.bufferedReader().readText()
			val error = process.errorStream.bufferedReader().readText()
			assertEquals(error, 0, process.exitValue())
			assertTrue(output, output.contains("[two words]"))
			assertTrue(output, output.contains("[]"))
			assertTrue(output, output.contains("[--check]"))
			assertTrue(output, output.contains("[PARENT:]"))
			assertTrue(output, output.contains("[EXPLICIT:two words中文]"))
			val check = run.copy(mode = "check", arguments = emptyList()).commandLine().createProcess().also { processes.add(it) }
			assertTrue(check.waitFor(10, TimeUnit.SECONDS))
			assertEquals(check.errorStream.bufferedReader().readText(), 0, check.exitValue())
		}
		finally
		{
			processes.forEach { process ->
				process.destroyForcibly().waitFor(5, TimeUnit.SECONDS)
				process.inputStream.close()
				process.errorStream.close()
				process.outputStream.close()
			}
			Files.deleteIfExists(directory.resolve(".verocache/arguments.vs.vsbc"))
			Files.deleteIfExists(directory.resolve(".verocache"))
			Files.deleteIfExists(program)
			Files.deleteIfExists(directory)
		}
	}
}

// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

package com.vanstysanic.veroscript.jetbrains.runtime

import com.intellij.openapi.project.Project
import com.vanstysanic.veroscript.jetbrains.VeroScriptBundle.message
import com.vanstysanic.veroscript.jetbrains.settings.SemanticOptions
import com.vanstysanic.veroscript.jetbrains.settings.VeroScriptApplicationSettings
import com.vanstysanic.veroscript.jetbrains.settings.VeroScriptLocalSettings
import java.nio.file.Files
import java.nio.file.Path
import java.util.concurrent.TimeUnit

data class RuntimeLayout(val home: Path) {
	private val suffix = if (System.getProperty("os.name").startsWith("Windows", true)) ".exe"
	else ""
	fun tool(name: String): Path {
		val path = home.resolve("veroscript$name$suffix")
		require(Files.isRegularFile(path) && (suffix.isNotEmpty() || Files.isExecutable(path))) {
			message("runtime.toolMissing", path)
		}
		return path
	}

	val stdlib: Path get() = home.resolve("stdlib")

	/** Call only from a background task; owns the short-lived version probe. */
	fun validate(): String {
		tool("-lsp")
		tool("-dap")
		require(Files.isDirectory(stdlib)) { message("runtime.stdlibMissing", stdlib) }
		require(Files.isDirectory(home.resolve("localization"))) { message("runtime.localizationMissing") }
		val process = ProcessBuilder(tool("").toString(), "--version").redirectErrorStream(true).start()
		return try {
			require(process.waitFor(5, TimeUnit.SECONDS)) { message("runtime.versionTimeout") }
			require(process.exitValue() == 0) { message("runtime.versionFailed") }
			process.inputStream.bufferedReader(Charsets.UTF_8).readText().take(4096)
		}
		finally
		{
			process.destroyForcibly()
		}
	}

	companion object {
		internal fun findOnPath(value: String): Path? {
			val suffix = if (System.getProperty("os.name").startsWith("Windows", true)) ".exe"
			else ""
			return value.split(java.io.File.pathSeparator)
				.filter { it.isNotBlank() }
				.mapNotNull { runCatching { Path.of(it.trim().removeSurrounding("\"")) }.getOrNull() }
				.firstOrNull {
					val cli = it.resolve("veroscript$suffix")
					Files.isRegularFile(cli) && (suffix.isNotEmpty() || Files.isExecutable(cli))
				}
		}

		fun resolve(project: Project, configuredHome: String? = null): RuntimeLayout {
			val personal = project.getService(VeroScriptLocalSettings::class.java).state.runtimeHome
			val application = com.intellij.openapi.application.ApplicationManager.getApplication()
				.getService(VeroScriptApplicationSettings::class.java).state.runtimeHome
			val configured = configuredHome ?: personal.ifBlank { application }
			val root = project.basePath?.let(Path::of)
			val home = if (configured.isNotBlank()) {
				SemanticOptions.absolute(configured, requireNotNull(root) { message("runtime.projectDirectory") })
			}
			else
			{
				findOnPath(System.getenv("PATH").orEmpty())
					?: error(message("runtime.configure"))
			}
			return RuntimeLayout(home.toAbsolutePath().normalize())
		}
	}
}

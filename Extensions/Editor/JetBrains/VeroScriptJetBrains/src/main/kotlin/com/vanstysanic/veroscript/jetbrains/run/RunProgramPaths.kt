// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

package com.vanstysanic.veroscript.jetbrains.run

import com.vanstysanic.veroscript.jetbrains.settings.SemanticOptions
import java.nio.file.Path

/** Context-created entries remain portable without changing the working directory's meaning. */
internal object RunProgramPaths {
	fun persist(file: Path, projectRoot: Path?): String {
		val absolute = file.toAbsolutePath().normalize()
		val root = projectRoot?.toAbsolutePath()?.normalize()
		return if (root != null && absolute.startsWith(root)) {
			"\$PROJECT_DIR\$/" + root.relativize(absolute).toString().replace(java.io.File.separatorChar, '/')
		}
		else absolute.toString()
	}

	fun resolve(options: VeroScriptRunOptions, projectRoot: Path,
		workingDirectory: Path = SemanticOptions.absolute(options.workingDirectory.orEmpty().ifBlank { "." }, projectRoot)): Path? =
		options.program.orEmpty().takeIf { it.isNotBlank() }?.let {
			SemanticOptions.absolute(it, workingDirectory, projectRoot)
		}
}

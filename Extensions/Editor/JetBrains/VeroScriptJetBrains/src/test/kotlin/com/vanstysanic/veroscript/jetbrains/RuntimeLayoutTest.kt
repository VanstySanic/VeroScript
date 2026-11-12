// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

package com.vanstysanic.veroscript.jetbrains

import com.vanstysanic.veroscript.jetbrains.runtime.RuntimeLayout
import org.junit.Assert.*
import org.junit.Test
import java.nio.file.Files
import java.nio.file.attribute.PosixFilePermissions

class RuntimeLayoutTest {
	@Test fun pathDiscoverySkipsInvalidEntriesAndAcceptsQuotedUnicodeDirectories() {
		val home = Files.createTempDirectory("vero path 中文 ")
		val windows = System.getProperty("os.name").startsWith("Windows", true)
		val cli = home.resolve(if (windows) "veroscript.exe"
		else "veroscript")
		try {
			Files.writeString(cli, "test")
			if (!windows) {
				assertNull(RuntimeLayout.findOnPath(home.toString()))
				Files.setPosixFilePermissions(cli, PosixFilePermissions.fromString("rwx------"))
			}
			val value = "\u0000invalid" + java.io.File.pathSeparator + "\"$home\""
			assertEquals(home, RuntimeLayout.findOnPath(value))
		}
		finally
		{
			Files.deleteIfExists(cli)
			Files.deleteIfExists(home)
		}
	}
}

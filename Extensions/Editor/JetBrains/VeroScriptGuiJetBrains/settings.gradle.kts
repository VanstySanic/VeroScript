// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

pluginManagement {
	repositories {
		gradlePluginPortal()
		mavenCentral()
	}
}
rootProject.name = "VeroScriptGuiJetBrains"
include(":veroscript")
project(":veroscript").projectDir = file("../VeroScriptJetBrains")

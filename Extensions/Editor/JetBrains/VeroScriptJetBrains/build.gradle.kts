// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

import org.jetbrains.intellij.platform.gradle.TestFrameworkType

plugins {
	kotlin("jvm") version "2.4.0"
	id("org.jetbrains.intellij.platform") version "2.19.0"
}

group = "com.vanstysanic.veroscript"
version = providers.gradleProperty("pluginVersion").get()

repositories {
	mavenCentral()
	intellijPlatform { defaultRepositories() }
}

dependencies {
	intellijPlatform {
		val localIde = providers.gradleProperty("localIdePath").orNull
		if (localIde != null) local(localIde)
		// Rider plugin development requires the multi-OS SDK archive.
		else rider(providers.gradleProperty("platformVersion").get()) { useInstaller = false }
		bundledModule("com.intellij.modules.lsp")
		bundledModule("com.intellij.modules.xdebugger")
		testFramework(TestFrameworkType.Platform)
	}
	testImplementation("junit:junit:4.13.2")
}

kotlin { jvmToolchain(25) }
sourceSets {
	main {
		resources.setSrcDirs(listOf("Resources", "Resources/Assets"))
		resources.include("META-INF/**", "Localization/**", "Icons/**")
		resources.exclude("META-INF/plugin.xml", "Icons/pluginIcon*.svg")
	}
	test { resources.setSrcDirs(listOf("Resources/Tests/Fixtures")) }
}
tasks.jar {
	from("Resources/Assets/Icons") {
		include("pluginIcon*.svg")
		into("META-INF")
	}
	from(rootDir.resolve("../../../../LICENSE")) {
		into("META-INF")
		rename { "LICENSE" }
	}
}
intellijPlatform {
	pluginConfiguration {
		name = "VeroScript"
		version = project.version.toString()
		ideaVersion {
			sinceBuild = "262.10315.191"
			untilBuild = provider { null }
		}
	}
	pluginVerification { ides { current() } }
}
tasks.buildPlugin { archiveBaseName.set("VeroScriptJetBrains") }
tasks.test {
	inputs.property("veroScriptTestRuntime", providers.environmentVariable("VEROSCRIPT_TEST_RUNTIME").orElse(""))
	inputs.property("veroScriptTestJitBackend", providers.environmentVariable("VEROSCRIPT_TEST_JIT_BACKEND").orElse(""))
	for (variable in listOf("VEROSCRIPT_TEST_RUNTIME", "VEROSCRIPT_TEST_JIT_BACKEND")) {
		inputs.files(providers.environmentVariable(variable).map { value ->
			if (value.isBlank()) emptyList()
			else listOf(file(value))
		}.orElse(emptyList())).withPropertyName("${variable}Files")
	}
}

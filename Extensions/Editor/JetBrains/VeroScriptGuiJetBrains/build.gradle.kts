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
		localPlugin(project(":veroscript"))
		testFramework(TestFrameworkType.Platform)
		testBundledModule("intellij.rider.rdclient.dotnet")
	}
	testImplementation("junit:junit:4.13.2")
}

kotlin { jvmToolchain(25) }
sourceSets {
	main {
		resources.setSrcDirs(listOf("Resources", "Resources/Assets"))
		resources.include("META-INF/**", "Localization/**", "Icons/**", "Catalog/**")
		resources.exclude("META-INF/plugin.xml", "Icons/pluginIcon*.svg")
	}
	test { resources.setSrcDirs(listOf("Resources/Tests/Fixtures")) }
}
tasks.jar {
	from("Resources/Assets/Icons") {
		include("pluginIcon*.svg")
		into("META-INF")
	}
	from(rootDir.resolve("../../../../LICENSE")) { into("META-INF") }
}
intellijPlatform {
	pluginConfiguration {
		name = "VeroScript GUI"
		version = project.version.toString()
		ideaVersion {
			sinceBuild = "262.10315.191"
			untilBuild = provider { null }
		}
	}
	pluginVerification { ides { current() } }
}
tasks.test {
	// Editor fixtures initialize Rider folder solutions without starting the .NET backend.
	systemProperty("rider.disable.resharper.backend", "true")
}
tasks.buildPlugin { archiveBaseName.set("VeroScriptGuiJetBrains") }

// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

plugins {
	id("com.android.library") version "8.9.2"
}

group = "com.vanstysanic.veroscript"
version = "0.1.1"

android {
	namespace = "com.vanstysanic.veroscript.android"
	compileSdk = 35
	ndkVersion = "28.2.13676358"
	defaultConfig {
		minSdk = 26
		ndk { abiFilters += listOf("arm64-v8a", "x86_64") }
		externalNativeBuild {
			cmake {
				arguments += listOf("-DVS_BUILD_VEROSCRIPT_EMBEDDED_ONLY=ON", "-DBUILD_TESTING=OFF",
					"-DVEROSCRIPT_BUILD_MOBILE_ADAPTERS=ON", "-DVEROSCRIPT_INTEROP_STATIC=OFF",
					"-DVEROSCRIPT_BUILD_ASMJIT_BACKEND=OFF", "-DVEROSCRIPT_BUILD_VM_BACKEND=ON",
					"-DVEROSCRIPT_INTEROP_DEFAULT_VM=ON", "-DANDROID_STL=c++_shared")
				arguments += "-DVEROSCRIPT_BUILD_APPLICATION=ON"
				targets += listOf("VeroScriptAndroid", "VeroScriptInterop")
			}
		}
		consumerProguardFiles("consumer-rules.pro")
	}
	externalNativeBuild {
		cmake {
			path = file("../../../../CMakeLists.txt")
			version = "3.31.6"
		}
	}
	compileOptions {
		sourceCompatibility = JavaVersion.VERSION_17
		targetCompatibility = JavaVersion.VERSION_17
	}
	buildFeatures { prefabPublishing = true }
	prefab {
		create("VeroScriptInterop") {
			headers = "../../../../VeroScriptInterop/Public"
		}
		create("VeroScriptAndroid") {
			headers = "Public"
		}
	}
	packaging { jniLibs.useLegacyPackaging = false }
	sourceSets["main"].assets.srcDir(layout.buildDirectory.dir("generated/veroscriptAssets"))
}

val prepareVeroScriptAssets by tasks.registering(Sync::class) {
	from("../../../../VeroScript/Resources/Scripts/stdlib")
	into(layout.buildDirectory.dir("generated/veroscriptAssets/veroscript/stdlib"))
}
val prepareVeroScriptNotices by tasks.registering(Sync::class) {
	from("../../../../LICENSE")
	from("../../../../VeroScriptApplication/ThirdParty/miniz/LICENSE") { rename { "MINIZ-LICENSE.txt" } }
	from("../../../../VeroScriptApplication/ThirdParty/json/LICENSE.MIT") { rename { "JSON-LICENSE.txt" } }
	from("../../../../VeroScriptApplication/ThirdParty/sha256/LICENSE") { rename { "SHA256-LICENSE.txt" } }
	from(androidComponents.sdkComponents.ndkDirectory.map { it.file("NOTICE") })
	from(androidComponents.sdkComponents.ndkDirectory.map { it.file("NOTICE.toolchain") })
	into(layout.buildDirectory.dir("generated/veroscriptAssets/veroscript/licenses"))
}
val prepareVeroScriptLocalization by tasks.registering(Sync::class) {
	from("../../../../VeroScript/Resources/Localization")
	from("../../../Language/Cpp/VeroScriptCpp/Resources/Localization")
	from("../../../../VeroScriptInterop/Resources/Localization")
	from("../VeroScriptMobile/Resources/Localization")
	duplicatesStrategy = DuplicatesStrategy.FAIL
	into(layout.buildDirectory.dir("generated/veroscriptAssets/veroscript/localization"))
}
tasks.named("preBuild") { dependsOn(prepareVeroScriptAssets, prepareVeroScriptNotices, prepareVeroScriptLocalization) }

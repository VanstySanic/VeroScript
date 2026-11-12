// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

plugins { id("com.android.library") version "8.9.2" }
group = "com.vanstysanic.veroscript"
val veroscriptAsmJitVersion = providers.gradleProperty("veroscriptAsmJitVersion").orElse("0.1.1")
require(Regex("[0-9]+\\.[0-9]+\\.[0-9]+").matches(veroscriptAsmJitVersion.get())) { "Invalid AsmJit release version" }
version = veroscriptAsmJitVersion.get()

android {
	namespace = "com.vanstysanic.veroscript.asmjit"
	compileSdk = 35
	ndkVersion = "28.2.13676358"
	defaultConfig {
		minSdk = 26
		ndk { abiFilters += listOf("arm64-v8a", "x86_64") }
		externalNativeBuild {
			cmake {
				arguments += listOf("-DVS_BUILD_VEROSCRIPT_EMBEDDED_ONLY=ON", "-DBUILD_TESTING=OFF",
					"-DVS_VEROSCRIPT_ASMJIT_VERSION=${veroscriptAsmJitVersion.get()}",
					"-DVEROSCRIPT_BUILD_ASMJIT_BACKEND=ON", "-DVEROSCRIPT_ENABLE_ASMJIT=ON",
					"-DVEROSCRIPT_BUILD_MOBILE_ADAPTERS=OFF", "-DANDROID_STL=c++_shared")
				targets += listOf("VeroScriptAsmJitBackend")
			}
		}
	}
	externalNativeBuild {
		cmake { path = file("../../../../../../CMakeLists.txt"); version = "3.31.6" }
	}
	// The matching base AAR owns the one shared STL. No duplicate Core/runtime.
	packaging { jniLibs.excludes += "**/libc++_shared.so" }
	sourceSets["main"].assets.srcDir(layout.buildDirectory.dir("generated/notices"))
}

val prepareNotices by tasks.registering(Sync::class) {
	from("../../../ThirdParty/asmjit/LICENSE.md")
	from("../../../ThirdParty/asmjit/VERSION.txt")
	from("../../../../../../LICENSE")
	into(layout.buildDirectory.dir("generated/notices/veroscript/licenses/asmjit"))
}
tasks.named("preBuild") { dependsOn(prepareNotices) }

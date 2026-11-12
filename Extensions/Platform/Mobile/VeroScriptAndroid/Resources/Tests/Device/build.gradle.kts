// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

plugins { id("com.android.application") version "8.9.2" }

val sdkAar = providers.gradleProperty("veroscriptAar").get()
val jitAar = providers.gradleProperty("veroscriptJitAar").orNull
android {
	namespace = "com.vanstysanic.veroscript.devicesmoke"
	compileSdk = 35
	ndkVersion = "28.2.13676358"
	defaultConfig {
		applicationId = "com.vanstysanic.veroscript.devicesmoke"
		minSdk = 26
		targetSdk = 35
		versionCode = 1
		versionName = "1"
		ndk { abiFilters += listOf("arm64-v8a", "x86_64") }
		buildConfigField("boolean", "WITH_JIT", (jitAar != null).toString())
		externalNativeBuild { cmake { arguments += "-DANDROID_STL=c++_shared" } }
	}
	buildTypes {
		getByName("release") {
			isMinifyEnabled = true
			signingConfig = signingConfigs.getByName("debug")
			proguardFiles(getDefaultProguardFile("proguard-android-optimize.txt"))
		}
	}
	compileOptions {
		sourceCompatibility = JavaVersion.VERSION_17
		targetCompatibility = JavaVersion.VERSION_17
	}
	externalNativeBuild { cmake { path = file("CMakeLists.txt"); version = "3.31.6" } }
	buildFeatures { prefab = true; buildConfig = true }
	// Explicit backend paths need extracted library files. The VM-only run also
	// checks loading the base SDK directly from a 16 KB-aligned APK.
	packaging { jniLibs.useLegacyPackaging = jitAar != null }
}
dependencies {
	implementation(files(sdkAar))
	if (jitAar != null) implementation(files(jitAar))
}
tasks.withType<JavaCompile>().configureEach { options.encoding = "UTF-8" }

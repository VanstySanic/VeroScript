// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include <VeroScriptAndroid.h>
#include <string>
#include <memory>
#include <stdexcept>
#include <unistd.h>
extern "C" uint32_t device_c_abi_version(void);

extern "C" JNIEXPORT jstring JNICALL Java_com_vanstysanic_veroscript_devicesmoke_DeviceSmoke_nativeIdentity(JNIEnv* env, jclass)
{
	const auto platform = vs_interop_get_host_platform();
	const auto version = vs_interop_get_runtime_version();
	std::string text(platform.data, platform.size);
	text += "/";
	text.append(version.data, version.size);
	text += "/abi=" + std::to_string(device_c_abi_version());
	return env->NewStringUTF(text.c_str());
}

extern "C" JNIEXPORT jlong JNICALL Java_com_vanstysanic_veroscript_devicesmoke_DeviceSmoke_nativePageSize(JNIEnv*, jclass)
{
	return sysconf(_SC_PAGESIZE);
}

extern "C" JNIEXPORT jobject JNICALL Java_com_vanstysanic_veroscript_devicesmoke_DeviceSmoke_nativeEmptyCatalog(JNIEnv* env, jclass)
{
	return veroscript_android_create_aot_programs(env, nullptr, nullptr, 0);
}

extern "C" JNIEXPORT jstring JNICALL Java_com_vanstysanic_veroscript_devicesmoke_DeviceSmoke_nativeJitProof(JNIEnv* env, jclass, jbyteArray path)
{
	try
	{
		auto require = [](bool condition, const char* error)
		{
			if (!condition)
			{
				throw std::runtime_error(error);
			}
		};
		std::string backend(static_cast<std::size_t>(env->GetArrayLength(path)), '\0');
		env->GetByteArrayRegion(path, 0, static_cast<jsize>(backend.size()), reinterpret_cast<jbyte*>(backend.data()));
		if (env->ExceptionCheck()) return nullptr;
		std::string output;
		vs_interop_session_options options{};
		options.struct_size = sizeof(options);
		options.user_data = &output;
		options.print_callback = [](vs_interop_string_view text, void* data)
		{
			static_cast<std::string*>(data)->append(text.data, text.size).append("\n");
		};
		vs_interop_session* raw = nullptr;
		require(vs_interop_session_create(&options, &raw) == VS_INTEROP_STATUS_OK, "C ABI session creation failed");
		const auto destroy = [](vs_interop_session* session)
		{
			vs_interop_session_destroy(session);
		};
		std::unique_ptr<vs_interop_session, decltype(destroy)> session(raw, destroy);
		require(vs_interop_session_set_jit_backend(raw, {backend.data(), backend.size()}) == VS_INTEROP_STATUS_OK,
				"Explicit Android JIT load failed");
		vs_interop_jit_backend_info info{};
		info.struct_size = sizeof(info);
		require(vs_interop_session_get_jit_backend_info(raw, &info) == VS_INTEROP_STATUS_OK && info.native_code,
				"Loaded backend does not expose native emission");
		vs_interop_config config{};
		config.struct_size = sizeof(config);
		config.execution_backend = VS_INTEROP_BACKEND_JIT;
		config.max_call_depth = 256;
		config.max_instructions = 1000000;
		config.timeout_milliseconds = 5000;
		config.enable_execution_control = 1;
		config.capability_policy_enabled = 1;
		config.allowed_script_capabilities = VS_INTEROP_SCRIPT_CAP_CONSOLE_OUTPUT;
		require(vs_interop_session_apply_config(raw, &config) == VS_INTEROP_STATUS_OK, "JIT config failed");
		vs_interop_jit_policy policy{};
		policy.struct_size = sizeof(policy);
		policy.mixed_hot_call_threshold = 1;
		policy.max_compile_attempts = 128;
		policy.max_cached_entries = 256;
		policy.eager_compile_budget = 64;
		policy.reuse_session_code = 1;
		require(vs_interop_session_set_jit_policy(raw, &policy) == VS_INTEROP_STATUS_OK, "JIT policy failed");
		const std::string source = "int kernel(int n){int s=0;for(int i=0;i<n;i++){s+=i;}return s;}void main(){print(kernel(100));}";
		const std::string label = "/veroscript/scripts/native-jit.vs";
		vs_interop_run_result result{};
		result.struct_size = sizeof(result);
		require(vs_interop_session_load_source(raw, {source.data(), source.size()}, {label.data(), label.size()}, &result)
				== VS_INTEROP_STATUS_OK && result.success, "JIT script load failed");
		require(vs_interop_session_run(raw, VS_INTEROP_RUN_SCRIPT, &result) == VS_INTEROP_STATUS_OK && result.success,
				"Native JIT run failed");
		require(output == "4950\n", "Native JIT output mismatch");
		vs_interop_jit_stats stats{};
		stats.struct_size = sizeof(stats);
		require(vs_interop_session_get_last_jit_stats(raw, &stats) == VS_INTEROP_STATUS_OK && stats.dispatches > 0 &&
				(stats.native_leaf_entries + stats.native_cfg_entries > 0) &&
				(stats.controlled_native_executions + stats.native_typed_loop_executions > 0),
				"Native execution counters are empty");
		const auto proof = "{\"dispatches\":" + std::to_string(stats.dispatches) +
			",\"nativeEntries\":" + std::to_string(stats.native_leaf_entries + stats.native_cfg_entries) +
			",\"controlledNativeExecutions\":" + std::to_string(stats.controlled_native_executions) +
			",\"nativeTypedLoopExecutions\":" + std::to_string(stats.native_typed_loop_executions) + "}";
		return env->NewStringUTF(proof.c_str());
	}
	catch (const std::exception& error)
	{
		env->ThrowNew(env->FindClass("java/lang/AssertionError"), error.what());
		return nullptr;
	}
}

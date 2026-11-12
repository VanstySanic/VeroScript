// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

using System.Reflection;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace VeroScript.Interop;

internal static unsafe partial class NativeMethods
{
	#if IOS
	// iOS AOT must see the static library name in P/Invoke metadata at build time.
	internal const string LibraryName = "__Internal";
	#else
	internal const string LibraryName = "VeroScriptInterop";
	#endif
	// Keep the managed binding baseline aligned with the native 0.1.1 ABI.
	internal const uint AbiVersion = 0x0000_0101;

	#if !IOS
	static NativeMethods()
	{
		NativeLibrary.SetDllImportResolver(typeof(NativeMethods).Assembly, ResolveLibrary);
	}
	#endif

	internal static void EnsureLoaded()
	{
		#if !IOS
		if (OperatingSystem.IsIOS())
		{
			throw new PlatformNotSupportedException(
				"iOS requires VeroScript.CSharp built for an iOS target framework and a static native reference.");
		}
		#endif
	}

	#if !IOS
	private static nint ResolveLibrary(string libraryName, Assembly assembly, DllImportSearchPath? searchPath)
	{
		if (libraryName != LibraryName)
		{
			return 0;
		}

		// Resolve once for all entry points so an environment change cannot mix native handles.
		lock (_libraryResolutionLock)
		{
			return _resolvedLibrary ??= LoadLibrary(assembly, searchPath);
		}
	}

	private static nint LoadLibrary(Assembly assembly, DllImportSearchPath? searchPath)
	{
		string? configuredPath = Environment.GetEnvironmentVariable("VEROSCRIPT_INTEROP_LIBRARY");
		if (!string.IsNullOrWhiteSpace(configuredPath))
		{
			if (!Path.IsPathFullyQualified(configuredPath))
			{
				throw new DllNotFoundException(
								"VEROSCRIPT_INTEROP_LIBRARY must contain an absolute library path.");
			}

			return NativeLibrary.Load(Path.GetFullPath(configuredPath));
		}

		// Android resolves packaged ABI-specific libraries through its native loader.
		if (OperatingSystem.IsAndroid()) return 0;

		string nativeFileName = OperatingSystem.IsWindows()
			? "VeroScriptInterop.dll"
			: OperatingSystem.IsMacOS()
				? "libVeroScriptInterop.dylib"
				: "libVeroScriptInterop.so";
		var adjacentLibrary = Path.Combine(AppContext.BaseDirectory, nativeFileName);
		if (File.Exists(adjacentLibrary))
		{
			return NativeLibrary.Load(adjacentLibrary);
		}

		return NativeLibrary.Load(LibraryName, assembly, searchPath);
	}
	#endif

	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern uint vs_interop_get_abi_version();
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern NativeStringView vs_interop_get_runtime_version();
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern NativeStringView vs_interop_get_host_platform();
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern ulong vs_interop_get_capabilities();
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern void vs_interop_buffer_destroy(nint buffer);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern NativeByteView vs_interop_buffer_view(nint buffer);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern VeroScriptStatus vs_interop_buffer_copy(nint buffer, byte* destination, ulong destinationSize, ulong* requiredSize);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern VeroScriptStatus vs_interop_locale_set_language(NativeStringView languageTag);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern VeroScriptStatus vs_interop_locale_translate(NativeStringView englishMessage, nint* translation);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern NativeStringView vs_interop_locale_get_language();
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern VeroScriptStatus vs_interop_locale_get_available_languages(nint* languagesJson);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern VeroScriptStatus vs_interop_locale_load_catalog_directory(NativeStringView directory, nint* error);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern VeroScriptStatus vs_interop_locale_merge_catalog_directory(NativeStringView directory, byte overrideExisting, nint* error);

	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern VeroScriptStatus vs_interop_session_create(NativeSessionOptions* options, nint* session);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern VeroScriptStatus vs_interop_session_destroy(nint session);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern VeroScriptStatus vs_interop_session_apply_config(nint session, NativeConfig* config);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern VeroScriptStatus vs_interop_session_apply_config_with_environment(nint session, NativeConfig* config, nint environment);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern VeroScriptStatus vs_interop_module_environment_retain(nint environment, nint* retained);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern void vs_interop_module_environment_release(nint environment);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern VeroScriptStatus vs_interop_session_request_cancel(nint session);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern VeroScriptStatus vs_interop_session_load_source(nint session, NativeStringView source, NativeStringView fileLabel, NativeRunResult* result);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern VeroScriptStatus vs_interop_session_load_file(nint session, NativeStringView path, NativeRunResult* result);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern VeroScriptStatus vs_interop_session_run(nint session, VeroScriptRunMode mode, NativeRunResult* result);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern VeroScriptStatus vs_interop_session_clear(nint session);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern byte vs_interop_session_is_loaded(nint session);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern VeroScriptStatus vs_interop_session_set_input(nint session, NativeStringView input);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern VeroScriptStatus vs_interop_session_set_hot_reload_enabled(nint session, byte enabled);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern byte vs_interop_session_is_hot_reload_enabled(nint session);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern VeroScriptStatus vs_interop_session_reload(nint session, NativeRunResult* result);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern VeroScriptStatus vs_interop_session_get_reload_status(nint session, NativeReloadStatus* status);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern VeroScriptStatus vs_interop_session_set_jit_policy(nint session, NativeJitPolicy* policy);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern VeroScriptStatus vs_interop_session_get_jit_backend_info(nint session, NativeJitBackendInfo* info);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern VeroScriptStatus vs_interop_session_set_jit_backend(nint session, NativeStringView path);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern VeroScriptStatus vs_interop_session_set_vm_backend(nint session, NativeStringView path);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern VeroScriptStatus vs_interop_session_get_vm_backend_info(nint session, NativeVmBackendInfo* info);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern VeroScriptStatus vs_interop_session_get_jit_backend(nint session, VeroScriptJitBackend* backend);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern VeroScriptStatus vs_interop_session_set_jit_cache_limits(nint session, NativeJitCacheLimits* limits);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern VeroScriptStatus vs_interop_session_get_jit_cache_limits(nint session, NativeJitCacheLimits* limits);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern VeroScriptStatus vs_interop_session_get_jit_cache_stats(nint session, NativeJitCacheStats* stats);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern VeroScriptStatus vs_interop_session_get_last_jit_stats(nint session, NativeJitStats* stats);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern VeroScriptStatus vs_interop_package_read(NativeByteView bytes, nint* package, nint* error);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern void vs_interop_package_destroy(nint package);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern VeroScriptPackageKind vs_interop_package_get_kind(nint package);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern NativeStringView vs_interop_package_get_name(nint package);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern ulong vs_interop_package_get_version(nint package);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern ulong vs_interop_package_metadata_count(nint package);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern VeroScriptStatus vs_interop_package_get_metadata(nint package, ulong index, NativePackageMetadata* metadata);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern NativeByteView vs_interop_package_get_canonical_signed_bytes(nint package);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern VeroScriptStatus vs_interop_session_load_package(nint session, nint package, NativeStringView logicalPath, NativeRunResult* result);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern VeroScriptStatus vs_interop_session_reload_package(nint session, nint package, NativeStringView logicalPath, NativeRunResult* result);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern VeroScriptStatus vs_interop_session_build_package_file(nint session, NativeStringView sourcePath, NativePackageBuildOptions* options, nint* packageBytes, NativeRunResult* result);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern VeroScriptStatus vs_interop_session_call_function(nint session, NativeStringView name, nint* arguments, ulong argumentCount, nint outReturn, NativeRunResult* result);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern VeroScriptStatus vs_interop_session_call_method(nint session, nint instance, NativeStringView name, nint* arguments, ulong argumentCount, nint outReturn, NativeRunResult* result);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern VeroScriptStatus vs_interop_session_call_method_bypassing_interceptor(nint session, nint instance, NativeStringView name, nint* arguments, ulong argumentCount, nint outReturn, NativeRunResult* result);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern VeroScriptStatus vs_interop_session_has_function(nint session, NativeStringView name, uint argumentCount, byte* hasFunction);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern VeroScriptStatus vs_interop_session_has_method(nint session, nint instance, NativeStringView name, uint argumentCount, byte* hasMethod);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern VeroScriptStatus vs_interop_session_construct_object(nint session, NativeStringView typeName, nint* arguments, ulong argumentCount, nint outObject, NativeRunResult* result);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern VeroScriptStatus vs_interop_session_get_field(nint session, nint instance, NativeStringView name, nint outValue, NativeRunResult* result);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern VeroScriptStatus vs_interop_session_set_field(nint session, nint instance, NativeStringView name, nint value, NativeRunResult* result);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern VeroScriptStatus vs_interop_session_get_global(nint session, NativeStringView name, nint outValue, NativeRunResult* result);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern VeroScriptStatus vs_interop_session_set_global(nint session, NativeStringView name, nint value, NativeRunResult* result);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern VeroScriptStatus vs_interop_session_register_native(nint session, NativeStringView name, NativeTypeDescriptor* returnType, NativeParameterDescriptor* parameters, ulong parameterCount, delegate* unmanaged[Cdecl]<nint, nint, nint, uint> callback, nint userData);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern VeroScriptStatus vs_interop_session_register_host_function(nint session, NativeHostFunctionDescriptor* function);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern VeroScriptStatus vs_interop_session_register_annotation(nint session, NativeAnnotationDescriptor* annotation);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern VeroScriptStatus vs_interop_session_register_host_type(nint session, NativeHostTypeDescriptor* type, uint* typeId);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern VeroScriptStatus vs_interop_session_register_host_member(nint session, uint typeId, NativeHostMemberDescriptor* member);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern VeroScriptStatus vs_interop_session_register_host_protocol(nint session, uint typeId, NativeHostTypeProtocol* protocol);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern VeroScriptStatus vs_interop_session_register_type_alias(nint session, NativeStringView name, NativeStringList typeParameters, NativeStringView targetTypeName, NativeStringView documentation);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern VeroScriptStatus vs_interop_session_register_generic_host_type_template(nint session, NativeStringView name, uint arity, NativeStringView documentation, delegate* unmanaged[Cdecl]<nint, nint, uint> callback, nint userData, uint* templateId);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern VeroScriptStatus vs_interop_session_instantiate_generic_host_type(nint session, uint templateId, NativeStringList typeArguments, uint* typeId);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern ulong vs_interop_session_diagnostic_count(nint session);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern VeroScriptStatus vs_interop_session_get_diagnostic(nint session, ulong index, NativeDiagnostic* diagnostic);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern VeroScriptStatus vs_interop_session_diagnostic_argument_count(nint session, ulong diagnosticIndex, ulong* count);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern VeroScriptStatus vs_interop_session_get_diagnostic_argument(nint session, ulong diagnosticIndex, ulong argumentIndex, NativeStringView* argument);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern VeroScriptStatus vs_interop_session_format_diagnostic(nint session, ulong diagnosticIndex, nint* text);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern VeroScriptStatus vs_interop_session_export_bindings_json(nint session, nint* json);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern VeroScriptStatus vs_interop_session_export_reflection_json(nint session, nint* json);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern VeroScriptStatus vs_interop_session_set_instance_call_interceptor(nint session, delegate* unmanaged[Cdecl]<nint, nint, nint, uint> interceptor, nint userData);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern VeroScriptStatus vs_interop_session_set_mutation_callback(nint session, delegate* unmanaged[Cdecl]<NativeMutationEvent*, nint, void> callback, nint userData);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern VeroScriptStatus vs_interop_session_apply_class_defaults(nint session, nint instance, NativeRunResult* result);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern VeroScriptStatus vs_interop_session_register_external_serializer(nint session, NativeStringView typeName, uint version, delegate* unmanaged[Cdecl]<nint, nint, uint> serializeCallback, delegate* unmanaged[Cdecl]<nint, nint, uint> deserializeCallback, nint userData);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern VeroScriptStatus vs_interop_session_serialize_object(nint session, nint instance, nint* bytes, uint* version);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern VeroScriptStatus vs_interop_session_deserialize_object(nint session, nint instance, NativeByteView bytes, uint encodedVersion);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern VeroScriptStatus vs_interop_session_enumerate_object_references(nint session, nint instance, delegate* unmanaged[Cdecl]<VeroScriptExternalReferenceKind, nint, nint, uint> visitor, nint userData);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern VeroScriptStatus vs_interop_session_copy_last_error(nint session, byte* buffer, ulong bufferSize, ulong* requiredSize);

	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern VeroScriptStatus vs_interop_value_create(nint* value);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern VeroScriptStatus vs_interop_value_clone(nint value, nint* clone);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern VeroScriptStatus vs_interop_value_assign(nint destination, nint source);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern void vs_interop_value_destroy(nint value);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern VeroScriptValueKind vs_interop_value_get_kind(nint value);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern VeroScriptStatus vs_interop_value_get_type_name(nint value, NativeStringView* name);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern VeroScriptStatus vs_interop_value_reset(nint value);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern VeroScriptStatus vs_interop_value_set_null(nint value);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern VeroScriptStatus vs_interop_value_is_null(nint value, byte* isNull);

	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern VeroScriptStatus vs_interop_value_set_int8(nint value, sbyte data);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern VeroScriptStatus vs_interop_value_get_int8(nint value, sbyte* data);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern VeroScriptStatus vs_interop_value_set_uint8(nint value, byte data);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern VeroScriptStatus vs_interop_value_get_uint8(nint value, byte* data);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern VeroScriptStatus vs_interop_value_set_int16(nint value, short data);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern VeroScriptStatus vs_interop_value_get_int16(nint value, short* data);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern VeroScriptStatus vs_interop_value_set_uint16(nint value, ushort data);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern VeroScriptStatus vs_interop_value_get_uint16(nint value, ushort* data);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern VeroScriptStatus vs_interop_value_set_int32(nint value, int data);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern VeroScriptStatus vs_interop_value_get_int32(nint value, int* data);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern VeroScriptStatus vs_interop_value_set_uint32(nint value, uint data);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern VeroScriptStatus vs_interop_value_get_uint32(nint value, uint* data);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern VeroScriptStatus vs_interop_value_set_int64(nint value, long data);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern VeroScriptStatus vs_interop_value_get_int64(nint value, long* data);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern VeroScriptStatus vs_interop_value_set_uint64(nint value, ulong data);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern VeroScriptStatus vs_interop_value_get_uint64(nint value, ulong* data);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern VeroScriptStatus vs_interop_value_set_float(nint value, float data);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern VeroScriptStatus vs_interop_value_get_float(nint value, float* data);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern VeroScriptStatus vs_interop_value_set_double(nint value, double data);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern VeroScriptStatus vs_interop_value_get_double(nint value, double* data);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern VeroScriptStatus vs_interop_value_set_bool(nint value, byte data);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern VeroScriptStatus vs_interop_value_get_bool(nint value, byte* data);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern VeroScriptStatus vs_interop_value_set_string(nint value, NativeStringView data);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern VeroScriptStatus vs_interop_value_get_string(nint value, NativeStringView* data);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern VeroScriptStatus vs_interop_value_set_wstring(nint value, char* data, ulong size);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern VeroScriptStatus vs_interop_value_get_wstring(nint value, char** data, ulong* size);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern VeroScriptStatus vs_interop_value_set_enum(nint value, NativeStringView typeName, long data);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern VeroScriptStatus vs_interop_value_get_enum(nint value, NativeStringView* typeName, long* data);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern VeroScriptStatus vs_interop_value_set_host_object(nint value, uint typeId, NativeStringView typeName, nint payload, delegate* unmanaged[Cdecl]<nint, void> destroy);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern VeroScriptStatus vs_interop_value_set_host_object_ex(nint value, uint typeId, NativeStringView typeName, nint payload, delegate* unmanaged[Cdecl]<nint, void> destroy, delegate* unmanaged[Cdecl]<nint, nint, NativeStringView, byte> isType, nint isTypeUserData);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern VeroScriptStatus vs_interop_value_get_host_object(nint value, uint* typeId, NativeStringView* typeName, nint* payload);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern VeroScriptStatus vs_interop_value_create_array(nint value, NativeStringView elementType, nint* elements, ulong count);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern VeroScriptStatus vs_interop_value_create_sequence(nint value, VeroScriptValueKind kind, NativeStringView elementType, nint* elements, ulong count);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern VeroScriptStatus vs_interop_value_sequence_count(nint value, ulong* count);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern VeroScriptStatus vs_interop_value_sequence_get(nint value, ulong index, nint element);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern VeroScriptStatus vs_interop_value_create_map(nint value, NativeStringView keyType, NativeStringView valueType, byte ordered, nint* keys, nint* values, ulong count);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern VeroScriptStatus vs_interop_value_create_map_kind(nint value, VeroScriptValueKind kind, NativeStringView keyType, NativeStringView valueType, nint* keys, nint* values, ulong count);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern VeroScriptStatus vs_interop_value_map_count(nint value, ulong* count);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern VeroScriptStatus vs_interop_value_map_get(nint value, nint key, nint mappedValue, byte* found);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern VeroScriptStatus vs_interop_value_map_enumerate(nint value, delegate* unmanaged[Cdecl]<nint, nint, nint, uint> visitor, nint userData);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern VeroScriptStatus vs_interop_value_create_set(nint value, NativeStringView elementType, nint* elements, ulong count);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern VeroScriptStatus vs_interop_value_create_set_kind(nint value, VeroScriptValueKind kind, NativeStringView elementType, nint* elements, ulong count);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern VeroScriptStatus vs_interop_value_set_count(nint value, ulong* count);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern VeroScriptStatus vs_interop_value_set_contains(nint value, nint element, byte* contains);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern VeroScriptStatus vs_interop_value_set_enumerate(nint value, delegate* unmanaged[Cdecl]<nint, nint, uint> visitor, nint userData);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern VeroScriptStatus vs_interop_value_create_channel(nint value, NativeStringView elementType);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern VeroScriptStatus vs_interop_value_create_bounded_channel(nint value, NativeStringView elementType, ulong capacity);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern VeroScriptStatus vs_interop_value_channel_send(nint channel, nint value, nint* error);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern VeroScriptStatus vs_interop_value_channel_try_send(nint channel, nint value, byte* sent);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern VeroScriptStatus vs_interop_value_channel_send_for(nint channel, nint value, ulong timeoutMilliseconds, byte* sent);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern VeroScriptStatus vs_interop_value_channel_receive(nint channel, nint value, nint* error);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern VeroScriptStatus vs_interop_value_channel_try_receive(nint channel, nint value, byte* received);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern VeroScriptStatus vs_interop_value_channel_receive_for(nint channel, ulong timeoutMilliseconds, nint value, byte* received);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern VeroScriptStatus vs_interop_value_channel_count(nint channel, ulong* count);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern VeroScriptStatus vs_interop_value_channel_capacity(nint channel, ulong* capacity);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern VeroScriptStatus vs_interop_value_channel_is_closed(nint channel, byte* closed);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern VeroScriptStatus vs_interop_value_channel_close(nint channel);

	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern VeroScriptStatus vs_interop_session_retain_callable(nint session, nint functionValue, byte ownerThreadOnly, byte weakThis, nint* callable);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern void vs_interop_callable_destroy(nint callable);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern byte vs_interop_callable_is_valid(nint callable);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern VeroScriptStatus vs_interop_callable_invoke(nint callable, nint* arguments, ulong argumentCount, nint outReturn, NativeRunResult* result);

	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern ulong vs_interop_call_context_argument_count(nint context);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern VeroScriptStatus vs_interop_call_context_get_argument(nint context, ulong index, nint* value);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern VeroScriptStatus vs_interop_call_context_get_mutable_argument(nint context, ulong index, nint* value);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern nint vs_interop_call_context_host_this(nint context);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern ulong vs_interop_call_context_program_generation(nint context);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern void vs_interop_call_context_set_error(nint context, NativeStringView error);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern VeroScriptStatus vs_interop_call_context_invoke_function(nint context, NativeStringView name, nint* arguments, ulong argumentCount, nint outReturn);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern VeroScriptStatus vs_interop_call_context_invoke_callable(nint context, nint functionValue, nint* arguments, ulong argumentCount, nint outReturn);

	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern nint vs_interop_intercept_context_receiver(nint context);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern VeroScriptStatus vs_interop_intercept_context_get_target(nint context, NativeResolvedMethod* target);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern ulong vs_interop_intercept_context_argument_count(nint context);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern VeroScriptStatus vs_interop_intercept_context_get_argument(nint context, ulong index, nint* value);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern VeroScriptStatus vs_interop_intercept_context_proceed(nint context, nint outReturn);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern void vs_interop_intercept_context_set_error(nint context, NativeStringView error);

	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern ulong vs_interop_generic_type_context_argument_count(nint context);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern VeroScriptStatus vs_interop_generic_type_context_get_argument(nint context, ulong index, NativeStringView* argument);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern VeroScriptStatus vs_interop_generic_type_context_set_type(nint context, NativeHostTypeDescriptor* type);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern VeroScriptStatus vs_interop_generic_type_context_add_member(nint context, NativeHostMemberDescriptor* member);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern VeroScriptStatus vs_interop_generic_type_context_set_protocol(nint context, NativeHostTypeProtocol* protocol);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern void vs_interop_generic_type_context_set_error(nint context, NativeStringView error);

	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern NativeStringView vs_interop_external_serialize_context_type_name(nint context);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern ulong vs_interop_external_serialize_context_mutation_version(nint context);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern ulong vs_interop_external_serialize_context_field_count(nint context);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern VeroScriptStatus vs_interop_external_serialize_context_get_field(nint context, ulong index, NativeStringView* name, NativeStringView* declaredType, NativeStringView* runtimeType, uint* fieldIndex, nint* value);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern VeroScriptStatus vs_interop_external_serialize_context_set_bytes(nint context, NativeByteView bytes);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern void vs_interop_external_serialize_context_set_error(nint context, NativeStringView error);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern NativeByteView vs_interop_external_deserialize_context_bytes(nint context);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern uint vs_interop_external_deserialize_context_encoded_version(nint context);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern VeroScriptStatus vs_interop_external_deserialize_context_add_field(nint context, NativeStringView fieldName, nint value);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern void vs_interop_external_deserialize_context_set_error(nint context, NativeStringView error);

	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern VeroScriptStatus vs_interop_session_set_annotation_service(nint session, nint service);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern VeroScriptStatus vs_interop_session_set_compile_processing_service(nint session, nint service);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern VeroScriptStatus vs_interop_compile_processing_service_retain(nint service, nint* retained);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern void vs_interop_compile_processing_service_release(nint service);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern VeroScriptStatus vs_interop_session_set_script_compile_options(nint session, NativeScriptCompileOptions* options);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern VeroScriptStatus vs_interop_session_get_diagnostic_severity(nint session, ulong index, uint* severity);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern ulong vs_interop_get_compile_capabilities();
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern VeroScriptStatus vs_interop_annotation_service_retain(nint service, nint* retained);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern void vs_interop_annotation_service_release(nint service);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern VeroScriptStatus vs_interop_session_retain_callback_user_data(nint session, nint userData, delegate* unmanaged[Cdecl]<nint, void> release);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern VeroScriptStatus vs_interop_aot_program_create(NativeAotDescription* description, NativeAotFunction* functions, ulong count, nint* program, nint* error);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern VeroScriptStatus vs_interop_aot_program_retain(nint program, nint* retained);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern void vs_interop_aot_program_release(nint program);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern VeroScriptStatus vs_interop_session_set_aot_program(nint session, nint program);


	#if !IOS
	private static readonly object _libraryResolutionLock = new();
	private static nint? _resolvedLibrary;
	#endif
}

// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

using System.Runtime.InteropServices;

namespace VeroScript.Interop;

[StructLayout(LayoutKind.Sequential)]
internal readonly unsafe struct NativeStringView
{
	internal readonly byte* Data;
	internal readonly ulong Size;

	internal NativeStringView(byte* data, ulong size)
	{
		Data = data;
		Size = size;
	}
}

[StructLayout(LayoutKind.Sequential)]
internal readonly unsafe struct NativeByteView
{
	internal readonly byte* Data;
	internal readonly ulong Size;
	internal NativeByteView(byte* data, ulong size)
	{
		Data = data;
		Size = size;
	}
}

[StructLayout(LayoutKind.Sequential)]
internal struct NativePackageMetadata
{
	internal NativeStringView Key;
	internal NativeByteView Value;
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct NativePackageBuildOptions
{
	internal uint StructSize;
	internal VeroScriptPackageKind Kind;
	internal NativeStringView PackageName;
	internal ulong PackageVersion;
	internal NativePackageMetadata* HostMetadata;
	internal ulong HostMetadataCount;
	internal byte StripDebugInfo;
	private fixed byte _reserved[7];
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct NativeStringList
{
	internal NativeStringView* Values;
	internal ulong Count;
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct NativePredefinedMacro
{
	internal uint StructSize;
	internal NativeStringView Name;
	internal NativeStringList Parameters;
	internal NativeStringView Body;
	internal NativeStringView SourceName;
	internal byte Variadic;
	private fixed byte _reserved[7];
}

[StructLayout(LayoutKind.Sequential)]
internal struct NativeSourceOverlay
{
	internal NativeStringView Path;
	internal NativeStringView Source;
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct NativeConfig
{
	internal uint StructSize;
	internal NativeStringList ImportPaths;
	internal NativeStringList ImplicitImportPaths;
	internal NativeStringList ScriptArguments;
	internal NativeStringList Defines;
	internal NativeStringList StandardLibrarySearchPaths;
	internal NativeSourceOverlay* SourceOverlays;
	internal ulong SourceOverlayCount;
	internal NativeStringView BytecodeCacheDirectory;
	internal ulong MaxCallDepth;
	internal VeroScriptExecutionBackend ExecutionBackend;
	internal byte UseBytecodeCache;
	internal byte AllowCircularImports;
	internal byte AllowProjectWideAccess;
	private byte _reserved;
	internal NativePredefinedMacro* PredefinedMacros;
	internal ulong PredefinedMacroCount;
	internal ulong MaxInstructions;
	internal ulong TimeoutMilliseconds;
	internal uint AllowedScriptCapabilities;
	internal byte EnableExecutionControl;
	internal byte CapabilityPolicyEnabled;
	private fixed byte _reservedV2[2];
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct NativeJitPolicy
{
	internal uint StructSize;
	internal uint MixedHotCallThreshold;
	internal ulong MaxCompileAttempts;
	internal ulong MaxCachedEntries;
	internal ulong EagerCompileBudget;
	internal byte ReuseSessionCode;
	internal byte EnableControlledMixed;
	private fixed byte _reserved[6];
}

[StructLayout(LayoutKind.Sequential)]
internal struct NativeJitCacheLimits
{
	internal uint StructSize;
	private uint _reserved;
	internal ulong MaxNativeBytes;
	internal ulong MaxRetainedPayloadBytes;
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct NativeJitCacheStats
{
	internal uint StructSize;
	private uint _reserved;
	internal ulong CompileAttempts;
	internal ulong RetainedPayloadBytes;
	internal ulong NativeBytes;
	internal byte ResourceLimitReached;
	private fixed byte _reservedBytes[7];
}

[StructLayout(LayoutKind.Sequential)]
internal struct NativeJitStats
{
	internal uint StructSize;
	private uint _reserved;
	internal ulong CompileAttempts, CacheHits, CompiledEntries, NativeLeafEntries, NativeCfgEntries;
	internal ulong BridgeTrampolineEntries, LinearTrampolineEntries, VmTrampolineEntries, UnsupportedEntries;
	internal ulong DispatchAttempts, Dispatches, Fallbacks, Failures, CacheInvalidations, HotnessDeferrals, HotFunctions;
	internal ulong CompileBudgetSkips, CacheCapacitySkips, EagerCompileSkips;
	internal ulong ArrayFastPathPushes, ArrayFastPathGets, ArrayFastPathSets;
	internal ulong ScalarMapFastPathGets, ScalarMapFastPathSets;
	internal ulong ScalarMethodFastPathI32, ScalarMethodFastPathU32, ScalarMethodFastPathF64;
	internal ulong ScalarFunctionFastPathI32, ScalarFunctionFastPathU32, ScalarFunctionFastPathF64;
	internal ulong ScalarNativeFastPathCalls, InlinedCallSites, LinearFrameReuses, LinearScratchReuses;
	internal ulong LinearScratchAllocations, LinearArgumentScratchLeases, LinearArgumentScratchReuses;
	internal ulong ScalarMethodFastPathMixed, ScalarFunctionFastPathMixed;
	internal ulong DirectNativeCalls;
	internal ulong CostModelVmFallbacks, NativeTypedLoopExecutions;
	internal ulong ControlledNativeExecutions, AotExecutions;
	internal ulong NativeLeafExecutions, NativeCfgExecutions, BridgeExecutions, LinearExecutions, VmTrampolineExecutions;
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct NativeSessionOptions
{
	internal uint StructSize;
	internal delegate* unmanaged[Cdecl]<NativeStringView, nint, void> PrintCallback;
	internal delegate* unmanaged[Cdecl]<NativeStringView, nint, void> DiagnosticCallback;
	internal nint UserData;
	internal delegate* unmanaged[Cdecl]<NativeConsoleInfo*, nint, byte> ConsoleQueryCallback;
	internal delegate* unmanaged[Cdecl]<uint, int, int, nint, byte> ConsoleControlCallback;
	internal delegate* unmanaged[Cdecl]<NativeStringView, nint, byte> ConsoleWriteCallback;
}

[StructLayout(LayoutKind.Sequential)]
internal struct NativeConsoleInfo
{
	internal uint StructSize;
	internal uint Flags;
	internal int Width;
	internal int Height;
}

[StructLayout(LayoutKind.Sequential)]
internal struct NativeRunResult
{
	internal uint StructSize;
	internal byte Success;
	internal byte ExitRequested;
	private ushort _reserved;
	internal int ExitCode;
	internal uint DiagnosticCount;
}

[StructLayout(LayoutKind.Sequential)]
internal struct NativeReloadStatus
{
	internal uint StructSize;
	internal VeroScriptReloadState State;
	internal ulong OperationId;
	internal ulong PreviousGeneration;
	internal ulong CandidateGeneration;
	internal ulong CurrentGeneration;
	internal ulong CommittedGeneration;
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct NativeTypeDescriptor
{
	internal uint StructSize;
	internal VeroScriptNativeTypeKind Type;
	internal NativeStringView Name;
	internal NativeTypeDescriptor* TypeArguments;
	internal ulong TypeArgumentCount;
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct NativeParameterDescriptor
{
	internal uint StructSize;
	internal NativeTypeDescriptor Type;
	internal VeroScriptParameterFlags Flags;
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct NativeAnnotationArgument
{
	internal NativeStringView Name;
	internal NativeStringView Value;
	internal byte HasValue;
	private fixed byte _reserved[7];
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct NativeAnnotation
{
	internal uint StructSize;
	internal NativeStringView Name;
	internal NativeAnnotationArgument* Arguments;
	internal ulong ArgumentCount;
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct NativeAnnotationList
{
	internal NativeAnnotation* Values;
	internal ulong Count;
}

[StructLayout(LayoutKind.Sequential)]
internal struct NativeAnnotationArgumentDescriptor
{
	internal uint StructSize;
	internal NativeStringView Name;
	internal NativeStringView Detail;
	internal NativeStringView Documentation;
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct NativeAnnotationDescriptor
{
	internal uint StructSize;
	internal NativeStringView Name;
	internal NativeStringView Detail;
	internal NativeStringView Documentation;
	internal NativeAnnotationArgumentDescriptor* Arguments;
	internal ulong ArgumentCount;
	internal uint NameFlags;
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct NativeHostFunctionDescriptor
{
	internal uint StructSize;
	internal NativeStringView Name;
	internal NativeTypeDescriptor ReturnType;
	internal NativeParameterDescriptor* Parameters;
	internal ulong ParameterCount;
	internal delegate* unmanaged[Cdecl]<nint, nint, nint, uint> Callback;
	internal nint UserData;
	internal NativeStringView Documentation;
	internal NativeStringList ParameterNames;
	internal NativeStringList ParameterDocumentation;
	internal NativeStringView ReturnDocumentation;
	internal NativeAnnotationList Annotations;
	internal NativeAnnotationList* ParameterAnnotations;
	internal ulong ParameterAnnotationCount;
	internal NativeStringList TypeParameters;
	internal NativeStringView ScriptReturnTypeName;
	internal byte IsGenericHostObjectCast;
	private fixed byte _reserved[7];
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct NativeHostTypeDescriptor
{
	internal uint StructSize;
	internal NativeStringView Name;
	internal VeroScriptHostTypeKind Kind;
	internal NativeStringView Documentation;
	internal NativeAnnotationList Annotations;
	internal byte Inheritable;
	internal byte ScopedEnum;
	private fixed byte _reserved[6];
	internal NativeStringView EnumUnderlyingTypeName;
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct NativeHostMemberDescriptor
{
	internal uint StructSize;
	internal VeroScriptHostMemberKind Kind;
	internal NativeStringView Name;
	internal NativeTypeDescriptor ReturnType;
	internal NativeParameterDescriptor* Parameters;
	internal ulong ParameterCount;
	internal delegate* unmanaged[Cdecl]<nint, nint, nint, uint> Callback;
	internal nint UserData;
	internal long EnumValue;
	internal NativeStringView Documentation;
	internal NativeStringList ParameterNames;
	internal NativeStringList ParameterDocumentation;
	internal NativeStringView ReturnDocumentation;
	internal NativeAnnotationList Annotations;
	internal NativeAnnotationList* ParameterAnnotations;
	internal ulong ParameterAnnotationCount;
	internal byte IsOperator;
	internal byte IsConstMethod;
	internal byte MutableThis;
	private fixed byte _reserved[5];
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct NativeHostTypeProtocol
{
	internal uint StructSize;
	internal delegate* unmanaged[Cdecl]<nint, nint, nint, uint> CloneCallback;
	internal nint CloneUserData;
	internal delegate* unmanaged[Cdecl]<nint, nint, nint, uint> EqualsCallback;
	internal nint EqualsUserData;
	internal delegate* unmanaged[Cdecl]<nint, nint, nint, uint> HashCallback;
	internal nint HashUserData;
	internal delegate* unmanaged[Cdecl]<nint, nint, nint, uint> SubclassFactoryCallback;
	internal nint SubclassFactoryUserData;
}

[StructLayout(LayoutKind.Sequential)]
internal struct NativeResolvedMethod
{
	internal uint StructSize;
	internal ulong ProgramGeneration;
	internal ulong ModuleIdentity;
	internal ulong DeclaringTypeIdentity;
	internal ulong MethodIdentity;
	internal uint FunctionIndex;
	internal NativeStringView ModuleName;
	internal NativeStringView DeclaringTypeName;
	internal NativeStringView MethodName;
	internal NativeStringView QualifiedName;
	internal byte IsVirtual;
	internal byte IsConst;
	internal byte ReturnsVoid;
	private byte _reserved;
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct NativeMutationEvent
{
	internal uint StructSize;
	internal nint Object;
	internal NativeStringView ObjectTypeName;
	internal NativeStringView FieldName;
	internal uint FieldIndex;
	internal VeroScriptMutationKind Kind;
	internal nint Value;
	internal ulong ObjectMutationVersion;
}

[StructLayout(LayoutKind.Sequential)]
internal struct NativeDiagnostic
{
	internal uint StructSize;
	internal NativeStringView Message;
	internal NativeStringView MessageId;
	internal NativeStringView FileLabel;
	internal uint Line;
	internal uint Column;
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct NativeJitBackendInfo
{
	internal uint StructSize;
	internal VeroScriptJitBackend Backend;
	internal NativeStringView Id;
	internal NativeStringView Target;
	internal ulong Capabilities;
	internal byte NativeCode;
	internal fixed byte Reserved[7];
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct NativeAotFunction
{
	internal ushort FunctionIndex;
	private ushort _reserved;
	private uint _reserved2;
	internal delegate* unmanaged[Cdecl]<nint, int> Entry;
	internal ulong GovernanceCapabilities;
}

[StructLayout(LayoutKind.Sequential)]
internal struct NativeAotDescription
{
	internal uint StructSize;
	// Product version encoded as major:8/minor:8/patch:16, independently of the Interop ABI.
	internal uint CoreVersion;
	internal NativeStringView ProgramDigest;
	internal NativeStringView NativeBindingsDigest;
	internal NativeStringView ServiceAbi;
	internal NativeStringView CoreImplementation;
	internal NativeStringView Target;
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct NativeVmBackendInfo
{
	public uint StructSize;
	public uint Flags;
	public NativeStringView Id;
	public NativeStringView Version;
	public NativeStringView Target;
	public NativeStringView LibraryPath;
	public ulong Capabilities;
}

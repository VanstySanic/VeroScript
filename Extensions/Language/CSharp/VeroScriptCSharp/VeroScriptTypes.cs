// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

namespace VeroScript;

/// <summary>Reports the transport-level result of an Interop ABI operation.</summary>
public enum VeroScriptStatus : uint
{
	Ok, InvalidArgument, OutOfMemory, WrongThread, EngineError, TypeMismatch,
	BufferTooSmall, CallbackError, Unsupported, InternalError,
	PermissionDenied, Expired, ReadOnly, Busy, LimitExceeded
}

/// <summary>Identifies optional features supported by the loaded native runtime.</summary>
/// <remarks>
/// These flags describe Core services. Query <see cref="VeroScriptSession.GetJitBackendInfo"/>
/// or <see cref="VeroScriptSession.GetVmBackendInfo"/> for the selected backend's own capabilities.
/// </remarks>
[Flags]
public enum VeroScriptCapabilities : ulong
{
	None = 0, Packages = 0x1, HotReload = 0x2, HostBindings = 0x4, BindingJson = 0x8,
	ReflectionJson = 0x10, InstanceIntercept = 0x20, MutationCallback = 0x40,
	ContainerValues = 0x80, RetainedCallables = 0x100, GenericHostTypes = 0x200,
	ExternalSerialization = 0x400, JitDiagnostics = 0x800, Localization = 0x1000,
	DistinctNumericPrimitives = 0x2000, ContainerEnumeration = 0x4000,
	ExecutionGovernance = 0x8000,
	SessionJitCache = 0x10000,
	NativeJit = 0x20000,
	NativeTypedLoops = 0x40000,
	JitBackendSelection = 0x80000,
	/// <summary>Supports one-shot module initialization with the selected execution backend.</summary>
	EvalOnce = 0x100000,
	Memory = 0x200000,
	/// <summary>Supports immutable module environment snapshots supplied by the native SDK.</summary>
	ModuleEnvironment = 0x400000,
	/// <summary>Supports the Core bigint/rational runtime; numeric values remain opaque host objects.</summary>
	Numerics = 0x800000,
	Variant = 0x1000000,
	/// <summary>Supports selecting and inspecting the session's independent VM backend.</summary>
	VmBackendSelection = 0x2000000,
	/// <summary>Supports querying how a staged hot reload settled.</summary>
	ReloadStatus = 0x4000000
}

/// <summary>Identifies compile-time services independently of execution and backend capabilities.</summary>
[Flags]
public enum VeroScriptCompileCapabilities : ulong
{
	None = 0,
	AnnotationProcessing = 1,
	ScriptExtensions = 2,
	GeneralProcessing = 4
}

/// <summary>Controls standard-library side effects when the capability policy is enabled.</summary>
[Flags]
public enum VeroScriptScriptCapabilities : uint
{
	None = 0,
	FileRead = 0x1,
	FileWrite = 0x2,
	FileDelete = 0x4,
	EnvironmentRead = 0x8,
	EnvironmentWrite = 0x10,
	ConsoleInput = 0x20,
	ConsoleOutput = 0x40,
	TaskSpawn = 0x80,
	ProcessDirectoryChange = 0x100,
	MemoryAddress = 0x200,
	All = FileRead | FileWrite | FileDelete | EnvironmentRead | EnvironmentWrite
		| ConsoleInput | ConsoleOutput | TaskSpawn | ProcessDirectoryChange | MemoryAddress
}

/// <summary>Selects incremental evaluation, script execution, or one-shot initialization.</summary>
public enum VeroScriptRunMode : uint
{
	Eval = 0,
	Script = 1,
	/// <summary>Runs module initialization without main and permits initializer JIT execution.</summary>
	/// <remarks>Ordinary sessions initialize fresh state on each run. Hot-reload sessions
	/// retain initialized globals and use the VM for retained initialization.
	/// Requires <see cref="VeroScriptCapabilities.EvalOnce"/>.</remarks>
	EvalOnce = 2
}
/// <summary>Selects the VM, mixed, or JIT execution backend.</summary>
public enum VeroScriptExecutionBackend : uint
{
	Vm,
	Mixed,
	Jit
}
/// <summary>Identifies an application or library package.</summary>
public enum VeroScriptPackageKind : uint
{
	Application = 1,
	Library = 2
}
/// <summary>Identifies an enum, struct, or class host type.</summary>
public enum VeroScriptHostTypeKind : uint
{
	Enum,
	Struct,
	Class
}
/// <summary>Identifies the role of a registered host member.</summary>
public enum VeroScriptHostMemberKind : uint
{
	Constructor, Method, StaticMethod, PropertyGetter, PropertySetter,
	EnumMember, Field, StaticConstant
}
/// <summary>Reports how an instance call interceptor handled a call.</summary>
public enum VeroScriptInterceptResult : uint
{
	Unhandled,
	Handled,
	Failed
}
/// <summary>Identifies the mutation represented by a mutation event.</summary>
public enum VeroScriptMutationKind : uint
{
	FieldWrite, ContainerAdd, ContainerRemove, ContainerSet, ContainerClear
}
/// <summary>Identifies a script or managed host reference during graph enumeration.</summary>
public enum VeroScriptExternalReferenceKind : uint
{
	ScriptObject,
	HostObject
}

/// <summary>Identifies the runtime kind stored in a <see cref="VeroScriptValue"/>.</summary>
public enum VeroScriptValueKind : uint
{
	Invalid, Int32, Int64, Double, String, Bool, Object, Array, UInt32, UInt64,
	WString, Map, Set, Queue, Stack, OrderedMap, Deque, Channel, Function, Enum,
	HostObject, NativeArray, Float, Int8, UInt8, Int16, UInt16, Null, MultiMap,
	OrderedMultiMap, PriorityQueue, MultiSet, OrderedMultiSet, Variant
}

/// <summary>Identifies a type accepted by native host binding declarations.</summary>
public enum VeroScriptNativeTypeKind : uint
{
	Void, Int32, Bool, Double, String, UInt32, HostObject, Array, Map, Set, Int64,
	UInt64, Enum, Queue, Stack, OrderedMap, Deque, Channel, MultiMap,
	OrderedMultiMap, PriorityQueue, MultiSet, OrderedMultiSet, Function, Float,
	Int8, UInt8, Int16, UInt16, Variant
}

/// <summary>Describes by-value, ref, or const-ref native parameters.</summary>
[Flags]
public enum VeroScriptParameterFlags : uint
{
	None = 0,
	Ref = 1,
	ConstRef = 2
}

/// <summary>Reports script execution, exit, and diagnostic state.</summary>
public readonly record struct VeroScriptRunResult(bool Success, bool ExitRequested, int ExitCode, uint DiagnosticCount);
/// <summary>Describes the most recent reload operation in a session.</summary>
public enum VeroScriptReloadState : uint
{
	None = 0,
	Rejected = 1,
	/// <summary>The candidate is staged and its initialization has yet to settle.</summary>
	Pending = 2,
	/// <summary>Candidate initialization failed and the previous program was restored.</summary>
	RolledBack = 3,
	Applied = 4,
	/// <summary>The candidate committed before an execution or cleanup error.</summary>
	AppliedWithErrors = 5,
	/// <summary>The session requires a reset before further execution.</summary>
	ResetRequired = 6
}

/// <summary>Contains the reload operation and program generations reported by the runtime.</summary>
/// <param name="State">The current reload state.</param>
/// <param name="OperationId">Identifies a reload independently of program generations.</param>
/// <param name="PreviousGeneration">The generation before the operation.</param>
/// <param name="CandidateGeneration">The replacement generation, when one was staged.</param>
/// <param name="CurrentGeneration">The currently loaded generation, which may still be pending.</param>
/// <param name="CommittedGeneration">The last committed generation.</param>
public readonly record struct VeroScriptReloadStatus(
	VeroScriptReloadState State, ulong OperationId, ulong PreviousGeneration,
	ulong CandidateGeneration, ulong CurrentGeneration, ulong CommittedGeneration);

/// <summary>Identifies the severity of a compiler or runtime diagnostic.</summary>
public enum VeroScriptDiagnosticSeverity : uint
{
	Error,
	Warning,
	Information,
	Hint
}
/// <summary>Contains one structured compiler or runtime diagnostic.</summary>
public readonly record struct VeroScriptDiagnostic(string Message, string MessageId, string FileLabel, uint Line, uint Column)
{
	/// <summary>Defaults to Error when an older same-ABI runtime cannot report severity.</summary>
	public VeroScriptDiagnosticSeverity Severity { get; init; }
}
/// <summary>Contains a typed enum value.</summary>
public readonly record struct VeroScriptEnumValue(string TypeName, long Value);
/// <summary>Contains one binary host-metadata entry from a package.</summary>
public readonly record struct VeroScriptPackageMetadata(string Key, byte[] Value);
/// <summary>Contains the managed payload and native identity of a host object.</summary>
public readonly record struct VeroScriptHostObjectInfo(uint TypeId, string TypeName, object Value);
/// <summary>Identifies a script method resolved for instance interception.</summary>
public readonly record struct VeroScriptResolvedMethod(
	ulong ProgramGeneration, ulong ModuleIdentity, ulong DeclaringTypeIdentity,
	ulong MethodIdentity, uint FunctionIndex, string ModuleName,
	string DeclaringTypeName, string MethodName, string QualifiedName,
	bool IsVirtual, bool IsConst, bool ReturnsVoid);

/// <summary>Represents a failed VeroScript Interop operation.</summary>
public sealed class VeroScriptException : Exception
{
	public VeroScriptStatus Status { get; }
	public VeroScriptException(VeroScriptStatus status, string message) : base(message) => Status = status;
}

/// <summary>Recursively describes a type used by a managed host binding.</summary>
public sealed record VeroScriptNativeType(VeroScriptNativeTypeKind Kind, string? Name = null, params VeroScriptNativeType[] TypeArguments)
{
	public static VeroScriptNativeType Void { get; } = new(VeroScriptNativeTypeKind.Void);
	public static VeroScriptNativeType Variant { get; } = new(VeroScriptNativeTypeKind.Variant);
	public static VeroScriptNativeType Int8 { get; } = new(VeroScriptNativeTypeKind.Int8);
	public static VeroScriptNativeType UInt8 { get; } = new(VeroScriptNativeTypeKind.UInt8);
	public static VeroScriptNativeType Int16 { get; } = new(VeroScriptNativeTypeKind.Int16);
	public static VeroScriptNativeType UInt16 { get; } = new(VeroScriptNativeTypeKind.UInt16);
	public static VeroScriptNativeType Int32 { get; } = new(VeroScriptNativeTypeKind.Int32);
	public static VeroScriptNativeType UInt32 { get; } = new(VeroScriptNativeTypeKind.UInt32);
	public static VeroScriptNativeType Int64 { get; } = new(VeroScriptNativeTypeKind.Int64);
	public static VeroScriptNativeType UInt64 { get; } = new(VeroScriptNativeTypeKind.UInt64);
	public static VeroScriptNativeType Float { get; } = new(VeroScriptNativeTypeKind.Float);
	public static VeroScriptNativeType Double { get; } = new(VeroScriptNativeTypeKind.Double);
	public static VeroScriptNativeType Bool { get; } = new(VeroScriptNativeTypeKind.Bool);
	public static VeroScriptNativeType String { get; } = new(VeroScriptNativeTypeKind.String);
}

/// <summary>Describes one native host function parameter.</summary>
public readonly record struct VeroScriptNativeParameter(VeroScriptNativeType Type, VeroScriptParameterFlags Flags = VeroScriptParameterFlags.None);
/// <summary>Implements a managed host function or member callback.</summary>
/// <remarks>VM workers may invoke the same delegate concurrently.</remarks>
public delegate void VeroScriptNativeFunction(VeroScriptCallContext context, VeroScriptBorrowedValue returnValue);
/// <summary>Intercepts one resolved script instance call.</summary>
/// <remarks>VM workers may invoke the same delegate concurrently.</remarks>
public delegate VeroScriptInterceptResult VeroScriptInstanceInterceptor(
	VeroScriptInterceptContext context, VeroScriptBorrowedValue returnValue);
/// <summary>Observes one script object or container mutation.</summary>
/// <remarks>VM workers may invoke the same delegate concurrently.</remarks>
public delegate void VeroScriptMutationHandler(VeroScriptMutationEvent mutation);
/// <summary>Constructs one concrete generic host type.</summary>
/// <remarks>The runtime invokes this delegate synchronously on the session owner thread.</remarks>
public delegate void VeroScriptGenericHostTypeFactory(VeroScriptGenericHostTypeContext context);
/// <summary>Encodes a script object into external bytes.</summary>
/// <remarks>The runtime invokes this delegate synchronously on the session owner thread.</remarks>
public delegate void VeroScriptExternalSerializeHandler(VeroScriptExternalSerializeContext context);
/// <summary>Reconstructs script fields from external bytes.</summary>
/// <remarks>The runtime invokes this delegate synchronously on the session owner thread.</remarks>
public delegate void VeroScriptExternalDeserializeHandler(VeroScriptExternalDeserializeContext context);
/// <summary>Visits one reference discovered in an externally serialized object.</summary>
/// <remarks>The runtime invokes this delegate synchronously on the session owner thread.</remarks>
public delegate void VeroScriptExternalReferenceVisitor(
	VeroScriptExternalReferenceKind kind, VeroScriptBorrowedValue value);

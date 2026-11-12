// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptMemory.h"
#include "VeroScriptBytecode.h"
#include "VeroScriptTypes.h"

#include <cstddef>
#include <cstdint>
#include <atomic>
#include <functional>
#include <string>
#include <vector>

namespace VeroScript
{
	class VeroScriptVm;

	enum class EDebugRunMode : std::uint8_t
	{
		Continue = 0,
		StepIn,
		StepOver,
		StepOut,
	};

	enum class EDebugStopReason : std::uint8_t
	{
		Breakpoint = 0,
		Step,
		Entry,
		Pause,
	};

	struct FDebugStackFrame
	{
		std::string FunctionName;
		std::string FileLabel;
		std::uint32_t Line = 1;
		std::uint32_t Column = 1;
		std::size_t InstructionPointer = 0;
	};

	struct FDebugVariable
	{
		enum class ETargetKind : std::uint8_t
		{
			None = 0,
			NativeArrayLocalElement,
			ArrayElement,
			QueueElement,
			StackElement,
			DequeElement,
			MapIntValue,
			MapUIntValue,
			MapInt64Value,
			MapUInt64Value,
			MapStringValue,
			MapWStringValue,
			MapEnumValue,
			MapHostValue,
			OrderedMapIntValue,
			OrderedMapUIntValue,
			OrderedMapInt64Value,
			OrderedMapUInt64Value,
			OrderedMapStringValue,
			OrderedMapWStringValue,
			OrderedMapEnumValue,
			OrderedMapHostValue,
			SetIntElement,
			SetUIntElement,
			SetInt64Element,
			SetUInt64Element,
			SetStringElement,
			SetWStringElement,
			SetEnumElement,
			ObjectField,
			// Appended to preserve the numeric values of the public target kinds.
			NativeArrayObjectFieldElement,
		};

		std::string Name;
		std::string Value;
		FMemoryInspection Memory;
		// Snapshot of the runtime value while paused, used for lazy structured inspection.
		FValue RawValue = std::int32_t{0};
		bool bHasRawValue = false;
		bool bReadOnly = false;
		// Mutation descriptor for setVariable. OwnerValue contains a shared handle.
		ETargetKind TargetKind = ETargetKind::None;
		FValue OwnerValue = std::int32_t{0};
		std::size_t TargetIndex = 0;
		FMapEntryKey TargetKey = std::int32_t{0};
		// Root local and nested indices for writable fixed-size native arrays.
		std::size_t RootFrameIndex = static_cast<std::size_t>(-1);
		std::size_t RootLocalSlot = static_cast<std::size_t>(-1);
		std::vector<std::size_t> NativeArrayPath;
	};

	struct FDebugEvaluateResult
	{
		bool bOk = false;
		std::string Value;
		std::string Error;
		FValue RawValue = std::int32_t{0};
		bool bHasRawValue = false;
	};

	/**
	 * Embedded debugger: line / conditional breakpoints, step modes,
	 * stack/locals, evaluate, set local.
	 * Attach to a VM before Execute; OnStop is invoked synchronously while the VM is paused.
	 */
	class VeroScriptDebugger
	{
	public:
		using FStopCallback = std::function<void(VeroScriptDebugger&, EDebugStopReason)>;

		void SetStopCallback(FStopCallback Callback);
		void SetRunMode(EDebugRunMode Mode);
		// May be called from a protocol thread while the VM is executing.
		void RequestPause() noexcept;
		void ResetPauseRequest() noexcept;
		EDebugRunMode GetRunMode() const
		{
			return RunMode;
		}

		// Breakpoint id. FileLabel empty matches any file. Condition optional.
		int SetBreakpoint(
			std::string FileLabel,
			std::uint32_t Line,
			std::string Condition = {});
		// Tooling has already resolved the exact compiled source label. Do not
		// match another module merely because its basename is the same.
		int SetExactBreakpoint(
			std::string FileLabel,
			std::uint32_t Line,
			std::string Condition = {});
		void ClearBreakpoints();

		std::vector<FDebugStackFrame> GetStackTrace() const;
		std::vector<FDebugVariable> GetLocals(std::size_t FrameIndex = 0) const;
		// Immediate children for Array / Map / Set values. Nested values are expanded lazily.
		std::vector<FDebugVariable> GetChildren(const FDebugVariable& Variable) const;

		// Evaluate expression in a paused frame (DAP-style FrameIndex, 0 = top).
		FDebugEvaluateResult Evaluate(
			std::string_view Expression,
			std::size_t FrameIndex = 0) const;

		/**
		 * Assign a local by name: RHS is evaluated as an expression in the frame,
		 * then written back to the VM local slot.
		 */
		FDebugEvaluateResult SetLocal(
			std::string_view Name,
			std::string_view ValueExpression,
			std::size_t FrameIndex = 0);
		FDebugEvaluateResult SetChild(
			const FDebugVariable& Variable,
			std::string_view ValueExpression,
			std::size_t FrameIndex = 0);

		// Called by VM; do not call from script.
		void AttachVm(VeroScriptVm* Vm);
		void DetachVm();
		bool ShouldStopBeforeInstruction(
			const FBytecodeModule& Module,
			const FBytecodeFunction* Function,
			std::size_t InstructionPointer,
			std::size_t CallDepth,
			FSourceLocation& OutLocation,
			EDebugStopReason& OutReason);
		void NotifyStopped(EDebugStopReason Reason);

	private:
		std::vector<FDebugVariable> GetChildrenImpl(const FDebugVariable& Variable) const;
		struct FBreakpoint
		{
			int Id = 0;
			std::string FileLabel;
			std::uint32_t Line = 0;
			std::string Condition;
			bool bExactFile = false;
		};

		static FSourceLocation ResolveLocation(
			const FBytecodeModule& Module,
			const FBytecodeFunction* Function,
			std::size_t InstructionPointer);
		static bool FileMatches(
			const std::string& BreakFile, std::string_view LocFile, bool bExactFile);
		int AddBreakpoint(
			std::string FileLabel,
			std::uint32_t Line,
			std::string Condition,
			bool bExactFile);

		bool ConditionHolds(const FBreakpoint& Breakpoint) const;

		VeroScriptVm* AttachedVm = nullptr;
		FStopCallback StopCallback;
		EDebugRunMode RunMode = EDebugRunMode::Continue;
		std::size_t StepTargetDepth = 0;
		bool bStopOnNext = false;
		bool bCaptureStepDepth = false;
		std::atomic<bool> bPauseRequested{false};
		// Re-entrancy guard while evaluating conditions / watches.
		mutable bool bInsideEvaluate = false;
		std::vector<FBreakpoint> Breakpoints;
		int NextBreakpointId = 1;
	};
}

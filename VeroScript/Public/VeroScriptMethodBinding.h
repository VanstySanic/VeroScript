// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptBytecode.h"
#include "VeroScriptNative.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace VeroScript
{
	class IHostBindingCatalog;

	struct FMethodBindingRequest
	{
		std::string ReceiverType;
		std::string MethodName;
		std::string FunctionType;
		bool bConstReceiver = false;
	};

	// A metadata view shared by the compiler and runtime. It owns its strings.
	struct FMethodBindingSelection
	{
		std::string OwnerType;
		std::string MethodName;
		std::string FunctionType;
		std::string LinkageName;
		std::string DispatchKey;
		std::uint16_t FunctionIndex = 0xFFFF;
		bool bPublic = false;
		bool bConst = false;
		bool bVirtual = false;
		bool bDeleted = false;
		bool bStatic = false;
		bool bVariadic = false;
		bool bHost = false;
	};

	// Compile-only lowering information. All names are owned; no runtime token or
	// borrowed AST survives compilation. Standalone emitters reuse this selection.
	struct FMethodBindingCodegenInfo
	{
		std::string SourceFile;
		std::uint32_t Line = 0, Column = 0;
		std::string FactoryName;
		std::string ReceiverType;
		std::string FunctionType;
		FMethodBindingSelection Selection;
		bool bConstantName = false;
		bool bWrappedReceiver = false;
		bool bConstReceiver = false;
	};

	inline const FMethodBindingCodegenInfo* FindMethodBindingCodegenInfo(
		const std::vector<FMethodBindingCodegenInfo>& Records, const FSourceLocation& Location)
	{
		const FMethodBindingCodegenInfo* Found = nullptr;
		for (const auto& Record : Records)
		{
			if (Record.SourceFile == Location.FileLabel && Record.Line == Location.Line &&
				Record.Column == Location.Column)
			{
				if (Found &&
					(Found->ReceiverType != Record.ReceiverType ||
						Found->FunctionType != Record.FunctionType ||
						Found->Selection.LinkageName != Record.Selection.LinkageName))
				{
					return nullptr;
				}

				Found = &Record;
			}
		}

		return Found;
	}

	struct FMethodBindingContext
	{
		const FBytecodeModule* Module = nullptr;
		const IHostBindingCatalog* Bindings = nullptr;
		const std::vector<FNativeFunction>* Natives = nullptr;
		std::shared_ptr<const void> ProgramOwner;
		std::uint64_t ProgramGeneration = 0;
		std::function<bool()> CheckWork;
	};

	struct FMethodBindingDescriptor
	{
		FMethodBindingRequest Request;
		FMethodBindingSelection Selection;
		std::shared_ptr<const void> ProgramOwner;
		std::uint64_t ProgramGeneration = 0;
		std::uint16_t NativeIndex = 0xFFFF;
		// Retains callback bridge/module ownership while the descriptor is live.
		FNativeFunction Native;
	};

	bool SelectBindableMethod(const FMethodBindingRequest& Request,
		const std::vector<FMethodBindingSelection>& Candidates, FMethodBindingSelection& OutSelection,
		std::string& OutError);

	FMethodBindingSelection MakeHostMethodBindingSelection(
		const struct FHostTypeDesc& Owner, const struct FHostMemberDesc& Member);

	bool CollectMethodBindingCandidates(const FBytecodeModule& Module, const IHostBindingCatalog* Bindings,
		const std::string& ReceiverType, std::vector<FMethodBindingSelection>& OutCandidates,
		std::string& OutError, const std::function<bool()>& CheckWork = {});

	bool ResolveMethodBinding(const FMethodBindingContext& Context, const FMethodBindingRequest& Request,
		std::shared_ptr<const FMethodBindingDescriptor>& OutDescriptor, std::string& OutError);

	bool ValidateMethodBinding(const FMethodBindingContext& Context,
		const FMethodBindingDescriptor& Descriptor, std::string& OutError);

	// The execution adapter must use the ordinary checked VM/native call service.
	// FunctionIndex/receiver have already undergone identity and virtual selection.
	using FBoundMethodInvoker = std::function<bool(const FMethodBindingDescriptor&,
		std::uint16_t, const FValue&, const std::vector<FValue>&, FValue&, std::string&)>;

	bool InvokeBoundMethod(const FMethodBindingContext& Context, const FMethodBindingDescriptor& Descriptor,
		const FValue& Receiver, const std::vector<FValue>& Arguments, const FBoundMethodInvoker& Invoker,
		FValue& OutResult, std::string& OutError);

	// Opaque core-owned carrier. Its payload cannot be manufactured by script.
	FHostObjectHandle MakeMethodBindingToken(
		std::shared_ptr<const FMethodBindingDescriptor> Descriptor);
	std::shared_ptr<const FMethodBindingDescriptor> ReadMethodBindingToken(const FValue& Value);
}

// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptCompile.h"
#include "VeroScriptHotReloadInternal.h"
#include "VeroScriptScriptCompileExtensions.h"
#include "VeroScriptVm.h"
#include <chrono>
#include <set>
#include <thread>

namespace VeroScript::ScriptExtensions
{
	struct FTask;
	struct FModule;
	struct FBridgeValue
	{
		std::string Type;
		std::map<std::string, FValue> Fields;
		std::weak_ptr<FTask> Task;
		std::uint64_t Epoch = 0;
		std::optional<FAnnotationDefinition> AnnotationDefinition;
	};
	struct FCall
	{
		std::function<FValue(const FNativeCallContext&)> Invoke;
	};
	struct FModule
	{
		std::string Path, Identity, Entry;
		std::uint64_t Generation = 0;
		std::shared_ptr<const FBytecodeModule> Code;
		std::unique_ptr<VeroScriptVm> Vm;
		FHotReloadState State;
		FFunctionHandle Registration;
		std::vector<FFunctionHandle> Callables;
	};
	struct FProcessor
	{
		FCompileProcessorDesc Description;
		std::shared_ptr<FModule> Module;
		FFunctionHandle Factory, Callback;
	};
	struct FTask final : IScriptCompileExtensionTask, std::enable_shared_from_this<FTask>
	{
		//~ Begin IScriptCompileExtensionTask Interface
		~FTask() override;
		const FCompileProcessingOptions& GetProcessingOptions() const override
		{
			return Processing;
		}
		bool Close(std::vector<FScriptError>&) override;
		bool MatchesPreparedProgram(const FPreparedProgram&, const IHostBindingCatalog*) override;
		//~ End IScriptCompileExtensionTask Interface
		void Check() const;
		FValue Invoke(FModule&, const FFunctionHandle&, const std::vector<FValue>&, std::string_view Entry = {});
		FHostObjectHandle Make(std::string Type, std::map<std::string, FValue> Fields = {}, bool Service = false);
		FBridgeValue& Value(const FValue&, std::string_view Type = {}) const;
		void Validate(const FBridgeValue&, std::string_view Type) const;
		FValue Read(std::string_view Path);
		FValue ReadResource(const FResourceRequest& Request);
		bool ReadDependency(std::string_view Path, std::string& Content);
		void RegisterAnnotation(const FValue&);
		void RegisterProcessor(const FValue&, const FValue&);
		void InitializeBindings();
		FHostObjectHandle Declaration(const FAnnotationDeclarationView*);
		FHostObjectHandle Document(const FSourceDocument*);
		FHostObjectHandle Scope(bool Member);
		FHostObjectHandle MetadataNode(const FSourceNodeMetadata*);
		void Load(const std::string& Path, const std::string& Entry, std::size_t Depth);
		std::vector<FSourceRange> Mask(std::string_view Module, std::string& Source, bool Discover = false);
		bool IsPathAllowed(std::string_view Path) const;
		std::string Resolve(std::string_view Path, std::string_view Importer, bool EnforceAllowedRoots = true) const;
		bool ReadSource(std::string_view Path, std::string& Source) const;
		void RecordContent(const std::string& Path, const std::string& Source);
		FHostObjectHandle CloneValue(const FBridgeValue&);

		FCompileOptions Inputs;
		FCompileProcessingOptions Processing;
		FHostBindingRegistry Bindings;
		std::vector<std::shared_ptr<FCall>> Calls;
		std::vector<std::shared_ptr<FModule>> Modules;
		std::vector<FProcessor> Processors;
		std::map<std::string, FAnnotationDependency> Dependencies;
		std::map<std::string, std::string> Contents;
		std::map<std::string, std::vector<FScriptCompileExtensionRoot>> ExtensionDependencies;
		std::set<std::string> Loaded, Visiting;
		std::shared_ptr<FModule> Executing;
		const FCompileProcessingContext* Context = nullptr;
		FCompileProcessingOutput* Output = nullptr;
		std::thread::id Thread = std::this_thread::get_id();
		std::uint64_t Epoch = 0, Instructions = 0;
		std::size_t BridgeBytes = 0, BridgeValues = 0, DependencyBytes = 0, ContentBytes = 0;
		std::chrono::steady_clock::time_point Deadline;
		bool Registering = false, Frozen = false, Closed = false;
		bool Cacheable = true;
		std::exception_ptr ExecutionFailure;
		std::deque<std::string> InputStrings;
	};
	FDynamicArrayHandle Array(std::string Element, std::vector<FValue> Values = {});
	std::string Text(const FValue&);
	std::int64_t Integer(const FValue&);
	const std::vector<FValue>& Elements(const FValue&);
	FValue Get(const FBridgeValue&, std::string_view Name);
	bool HasCompilerImport(std::string_view Source);
	struct FCompiledExtension
	{
		std::shared_ptr<const FBytecodeModule> Code;
		std::shared_ptr<const FPreparedProgram> Prepared;
	};
	FCompiledExtension CompileBootstrap(FTask&, const std::string& Path, const std::string& Source,
										const FCompileOptions&);
} // namespace VeroScript::ScriptExtensions

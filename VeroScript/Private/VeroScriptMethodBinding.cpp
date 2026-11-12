// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptMethodBinding.h"

#include "VeroScriptHostBinding.h"
#include "VeroScriptMethodKey.h"
#include "VeroScriptMethodBindingInternal.h"
#include "VeroScriptModuleMeta.h"
#include "VeroScriptRuntime.h"

#include <algorithm>
#include <limits>

namespace VeroScript
{
	namespace
	{
		constexpr std::string_view TokenType = "__std_method_binding";
		constexpr std::uint32_t TokenTypeId = 0xFFFFFFF0u;

		std::string NativeSignature(const FNativeFunction& Native, std::size_t First = 0)
		{
			std::vector<std::string> Parameters;
			for (std::size_t Index = First; Index < Native.ParamTypes.size(); ++Index)
			{
				std::string Type = ResolveNativeParamTypeName(Native, Index);
				const auto Flags = Index < Native.ParamFlags.size() ? Native.ParamFlags[Index] : 0;
				if (Flags & 1)
				{
					Type = (Flags & 2 ? "const ref " : "ref ") + Type;
				}

				Parameters.push_back(std::move(Type));
			}
			return Runtime::MakeFunctionTypeName(Parameters, ResolveNativeReturnTypeName(Native));
		}

		bool PublicMethod(const FBytecodeModule& Module, const std::string& Receiver,
		    const FInstanceMethodInfo& Method, const FBytecodeFunction& Function)
		{
			using namespace MethodBindingDetail;
			if (Function.Access == EMemberAccess::Private) return false;
			std::uint16_t Root = 0, Declaring = 0;
			if (!ModuleMeta::FindTypeIndex(Module, Receiver, Root))
			{
				return Function.Access == EMemberAccess::Public;
			}

			if (!ModuleMeta::FindTypeIndex(Module, Method.DeclaringTypeName, Declaring))
			{
				return Function.Access == EMemberAccess::Public;
			}

			std::unordered_set<std::uint16_t> Visited;
			if (Function.Access == EMemberAccess::Public &&
			    HasPublicBasePath(Module, Root, Declaring, Visited))
			{
				return true;
			}
			const auto& Type = Module.Types[Root];
			for (const auto& Using : Type.UsingMembers)
			{
				if (Using.Access == EMemberAccess::Public && Using.MemberName == Method.Name)
				{
					for (std::size_t Index = 0; Index < Type.BaseTypeIndices.size(); ++Index)
					{
						Visited.clear();
						if (UsingNamesDirectBase(Module, Type, Using, Index) &&
						    HasBasePath(Module, Type.BaseTypeIndices[Index], Declaring, Visited))
						{
							return true;
						}
					}
				}
			}

			return false;
		}

		bool CheckWork(const FMethodBindingContext& Context, std::string& Error)
		{
			if (!Context.CheckWork || Context.CheckWork()) return true;
			Error = "method binding interrupted";
			return false;
		}
	}

	bool SelectBindableMethod(const FMethodBindingRequest& Request,
	    const std::vector<FMethodBindingSelection>& Candidates, FMethodBindingSelection& Out,
	    std::string& Error)
	{
		Error.clear();
		std::vector<std::string> Params;
		std::string Result;
		const auto Start = [](unsigned char C)
		{
			return C == '_' || (C >= 'a' && C <= 'z') || (C >= 'A' && C <= 'Z');
		};
		const bool Identifier = !Request.MethodName.empty() &&
		    Start(static_cast<unsigned char>(Request.MethodName[0])) &&
		    std::all_of(Request.MethodName.begin(), Request.MethodName.end(),
		        [&](unsigned char C)
		{
			return Start(C) || (C >= '0' && C <= '9');
		});
		if (Request.ReceiverType.empty() || !Identifier ||
		    !Runtime::ParseFunctionType(Request.FunctionType, Params, Result))
		{
			Error = "invalid method binding request";
			return false;
		}
		const FMethodBindingSelection* Best = nullptr;
		bool Ambiguous = false;
		for (const auto& Candidate : Candidates)
		{
			if (Candidate.MethodName != Request.MethodName || !Candidate.bPublic || Candidate.bStatic ||
			    Candidate.bDeleted || Candidate.bVariadic || Candidate.FunctionType != Request.FunctionType ||
			    (Request.bConstReceiver && !Candidate.bConst))
			{
				continue;
			}

			if (!Best || (!Request.bConstReceiver && Best->bConst && !Candidate.bConst))
			{
				Best = &Candidate;
				Ambiguous = false;
			}
			else if (Best->bConst == Candidate.bConst &&
			    (Best->LinkageName != Candidate.LinkageName || Best->OwnerType != Candidate.OwnerType))
			{
				Ambiguous = true;
			}
		}
		if (!Best)
		{
			Error = "no public instance method matches " + Request.ReceiverType + "." + Request.MethodName + " as " + Request.FunctionType;
		}
		else if (Ambiguous)
		{
			Error = "ambiguous method binding: " + Request.ReceiverType + "." + Request.MethodName;
		}
		else
		{
			Out = *Best;
			return true;
		}
		return false;
	}

	FMethodBindingSelection MakeHostMethodBindingSelection(
	    const FHostTypeDesc& Owner, const FHostMemberDesc& Member)
	{
		FNativeFunction Native;
		Native.HostTypeName = Owner.Name;
		Native.ReturnType = Member.ReturnType;
		Native.ReturnHostTypeName = Member.ReturnHostTypeName;
		Native.ReturnElementTypeName = Member.ReturnElementTypeName;
		Native.ReturnKeyTypeName = Member.ReturnKeyTypeName;
		Native.ReturnValueTypeName = Member.ReturnValueTypeName;
		Native.ParamTypes = Member.ParamTypes;
		Native.ParamHostTypeNames = Member.ParamHostTypeNames;
		Native.ParamElementTypeNames = Member.ParamElementTypeNames;
		Native.ParamKeyTypeNames = Member.ParamKeyTypeNames;
		Native.ParamValueTypeNames = Member.ParamValueTypeNames;
		Native.ParamFlags = Member.ParamFlags;
		FMethodBindingSelection Out;
		Out.OwnerType = Owner.Name;
		Out.MethodName = Member.Name;
		Out.FunctionType = NativeSignature(Native);
		Out.LinkageName = Member.NativeName.empty()
		    ? MakeHostMethodNativeName(Owner.Name, Member.Name)
		    : Member.NativeName;
		Out.bPublic = Member.Kind == EHostMemberKind::Method && !Member.bOperator;
		Out.bStatic = Member.Kind == EHostMemberKind::StaticMethod;
		Out.bConst = Member.bConstMethod;
		Out.bHost = true;
		return Out;
	}

	bool CollectMethodBindingCandidates(const FBytecodeModule& Module, const IHostBindingCatalog* Bindings,
	    const std::string& ReceiverType, std::vector<FMethodBindingSelection>& Candidates, std::string& Error,
	    const std::function<bool()>& Work)
	{
		Candidates.clear();
		Error.clear();
		FMethodBindingContext Context;
		Context.Module = &Module;
		Context.Bindings = Bindings;
		Context.CheckWork = Work;
		std::vector<FInstanceMethodInfo> Methods;
		ModuleMeta::EnumerateInstanceMethods(Module, ReceiverType, Methods);
		for (const auto& Method : Methods)
		{
			if (!CheckWork(Context, Error)) return false;
			if (Method.FunctionIndex >= Context.Module->Functions.size())
			{
				continue;
			}

			const auto& Function = Context.Module->Functions[Method.FunctionIndex];
			if (!Function.TemplateInstance.Arguments.empty())
			{
				continue;
			}
			FMethodBindingSelection Candidate;
			Candidate.OwnerType = Method.DeclaringTypeName;
			Candidate.MethodName = Method.Name;
			Candidate.FunctionType = Function.ClosureTypeName;
			Candidate.FunctionIndex = Method.FunctionIndex;
			Candidate.LinkageName = Function.Name;
			const auto Position = FindMethodLinkageSeparator(Function.Name);
			Candidate.DispatchKey =
			    Position == std::string::npos ? Function.Name : Function.Name.substr(Position + 2);
			Candidate.bPublic = PublicMethod(Module, ReceiverType, Method, Function);
			Candidate.bConst = Function.bIsConstMethod;
			Candidate.bVirtual = Function.bIsVirtual;
			Candidate.bDeleted = Function.bDeleted;
			Candidate.bStatic = Function.bIsStaticMethod;
			Candidates.push_back(std::move(Candidate));
		}
		if (Context.Bindings)
		{
			const FHostTypeDesc* Host = Context.Bindings->FindType(ReceiverType);
			if (!Host)
			{
				std::uint16_t TypeIndex = 0;
				if (ModuleMeta::FindTypeIndex(*Context.Module, ReceiverType, TypeIndex))
				{
					Host = Context.Bindings->FindType(Context.Module->Types[TypeIndex].HostBaseTypeName);
				}
			}
			if (Host)
			{
				for (const auto& Member : Host->Members)
				{
					if (!CheckWork(Context, Error)) return false;
					if (std::none_of(Candidates.begin(), Candidates.end(),
					        [&](const auto& Candidate)
					{
						return !Candidate.bHost && Candidate.MethodName == Member.Name;
					}))
					{
						auto Candidate = MakeHostMethodBindingSelection(*Host, Member);
						std::string Canonical;
						if (!ResolveHostRuntimeTypeName(*Bindings, Candidate.FunctionType, Canonical, Error))
						{
							return false;
						}
						Candidate.FunctionType = std::move(Canonical);
						Candidates.push_back(std::move(Candidate));
					}
				}
			}
		}
		return true;
	}

	bool ResolveMethodBinding(const FMethodBindingContext& Context, const FMethodBindingRequest& Request,
	    std::shared_ptr<const FMethodBindingDescriptor>& Out, std::string& Error)
	{
		Out.reset();
		Error.clear();
		if (!Context.Module || !Context.ProgramOwner || !Context.ProgramGeneration ||
		    !CheckWork(Context, Error))
		{
			if (Error.empty())
			{
				Error = "method binding requires an active program";
			}

			return false;
		}
		std::vector<FMethodBindingSelection> Candidates;
		if (!CollectMethodBindingCandidates(*Context.Module, Context.Bindings, Request.ReceiverType,
		        Candidates, Error, Context.CheckWork))
		{
			return false;
		}

		auto Descriptor = std::make_shared<FMethodBindingDescriptor>();
		if (!SelectBindableMethod(Request, Candidates, Descriptor->Selection, Error)) return false;
		std::uint16_t InterfaceIndex = 0;
		const bool InterfaceReceiver =
		    ModuleMeta::FindInterfaceIndex(*Context.Module, Request.ReceiverType, InterfaceIndex);
		if (!Descriptor->Selection.bHost && !InterfaceReceiver)
		{
			std::vector<std::string> Parameters;
			std::string ReturnType;
			Runtime::ParseFunctionType(Request.FunctionType, Parameters, ReturnType);
			std::uint16_t Index = 0xFFFF;
			if (ModuleMeta::TryResolveInstanceMethodOnType(*Context.Module, Request.ReceiverType,
			        Descriptor->Selection.DispatchKey, static_cast<std::uint8_t>(Parameters.size()),
			        Index) != EMethodResolveStatus::Found ||
			    Index != Descriptor->Selection.FunctionIndex)
			{
				Error = "method binding does not identify a unique visible method";
				return false;
			}
		}
		Descriptor->Request = Request;
		Descriptor->ProgramOwner = Context.ProgramOwner;
		Descriptor->ProgramGeneration = Context.ProgramGeneration;
		if (Descriptor->Selection.bHost)
		{
			if (Context.Natives)
			{
				for (std::size_t Index = 0; Index < Context.Natives->size(); ++Index)
				{
					if ((*Context.Natives)[Index].Name == Descriptor->Selection.LinkageName &&
					    Index < std::numeric_limits<std::uint16_t>::max())
					{
						const auto& Native = (*Context.Natives)[Index];
						if (!Native.Callback || NativeSignature(Native, 1) != Request.FunctionType)
						{
							continue;
						}

						Descriptor->NativeIndex = static_cast<std::uint16_t>(Index);
						Descriptor->Native = Native;
						break;
					}
				}
			}

			if (Descriptor->NativeIndex == 0xFFFF)
			{
				Error = "method binding requires a live native registration";
				return false;
			}
		}
		Out = std::move(Descriptor);
		return true;
	}

	bool ValidateMethodBinding(
	    const FMethodBindingContext& Context, const FMethodBindingDescriptor& Descriptor, std::string& Error)
	{
		Error.clear();
		if (!CheckWork(Context, Error)) return false;
		if (!Context.Module || !Context.ProgramOwner || Descriptor.ProgramOwner != Context.ProgramOwner ||
		    Descriptor.ProgramGeneration != Context.ProgramGeneration)
		{
			Error = "method binding is stale or belongs to another program";
			return false;
		}
		const auto& Selection = Descriptor.Selection;
		if (Selection.bHost)
		{
			if (!Context.Natives || Descriptor.NativeIndex >= Context.Natives->size())
			{
				Error = "method binding native registration no longer exists";
				return false;
			}
			const auto& Native = (*Context.Natives)[Descriptor.NativeIndex];
			if (!Native.Callback || Native.Name != Descriptor.Native.Name ||
			    Native.Callback != Descriptor.Native.Callback ||
			    Native.UserData != Descriptor.Native.UserData ||
			    Native.CallbackOwner != Descriptor.Native.CallbackOwner ||
			    Native.ParamFlags != Descriptor.Native.ParamFlags ||
			    Native.ParamTypes != Descriptor.Native.ParamTypes ||
			    Native.bHostStaticMethod != Descriptor.Native.bHostStaticMethod ||
			    ResolveNativeParamTypeName(Native, 0) != ResolveNativeParamTypeName(Descriptor.Native, 0) ||
			    NativeSignature(Native, 1) != Descriptor.Request.FunctionType)
			{
				Error = "method binding native registration changed";
				return false;
			}
		}
		else if (Selection.FunctionIndex >= Context.Module->Functions.size() ||
		    Context.Module->Functions[Selection.FunctionIndex].Name != Selection.LinkageName ||
		    Context.Module->Functions[Selection.FunctionIndex].ClosureTypeName !=
		        Descriptor.Request.FunctionType ||
		    Context.Module->Functions[Selection.FunctionIndex].bDeleted)
		{
			Error = "method binding identity no longer matches the program";
			return false;
		}
		return true;
	}

	bool InvokeBoundMethod(const FMethodBindingContext& Context, const FMethodBindingDescriptor& Descriptor,
	    const FValue& Receiver, const std::vector<FValue>& Arguments, const FBoundMethodInvoker& Invoker,
	    FValue& OutResult, std::string& Error)
	{
		if (!ValidateMethodBinding(Context, Descriptor, Error)) return false;
		std::vector<std::string> Params;
		std::string Result;
		if (!Invoker || !Runtime::ParseFunctionType(Descriptor.Request.FunctionType, Params, Result) ||
		    Params.size() != Arguments.size())
		{
			Error = "bound method argument signature mismatch";
			return false;
		}
		FValue ActualReceiver = Receiver;
		const auto& Selection = Descriptor.Selection;
		std::uint16_t FunctionIndex = Selection.FunctionIndex;
		if (const auto* Script = std::get_if<FObjectHandle>(&Receiver); Script && *Script)
		{
			if (((*Script)->ProgramOwner && (*Script)->ProgramOwner != Context.ProgramOwner) ||
			    !ModuleMeta::IsObjectTypeAssignableTo(
			        *Context.Module, (*Script)->GetTypeName(), Descriptor.Request.ReceiverType))
			{
				Error = "bound method receiver belongs to an incompatible type or program";
				return false;
			}
			if (Selection.bHost)
			{
				ActualReceiver = (*Script)->GetHostBase();
			}

			else if (Selection.bVirtual)
			{
				if (ModuleMeta::TryResolveInstanceMethodOnType(*Context.Module, (*Script)->GetTypeName(),
				        Selection.DispatchKey, static_cast<std::uint8_t>(Params.size()),
				        FunctionIndex) != EMethodResolveStatus::Found)
				{
					Error = "bound virtual method has no unique implementation";
					return false;
				}
			}
		}
		else if (!Selection.bHost)
		{
			Error = "bound method requires a non-null script receiver";
			return false;
		}
		if (Selection.bHost)
		{
			const auto* Host = std::get_if<FHostObjectHandle>(&ActualReceiver);
			if (!Host || !*Host || !HostObjectMatchesType(*Host, Selection.OwnerType))
			{
				Error = "bound method requires a compatible non-null host receiver";
				return false;
			}
		}
		return Invoker(Descriptor, FunctionIndex, ActualReceiver, Arguments, OutResult, Error);
	}

	FHostObjectHandle MakeMethodBindingToken(std::shared_ptr<const FMethodBindingDescriptor> Descriptor)
	{
		if (!Descriptor) return {};
		auto Token = std::make_shared<FHostObject>();
		Token->TypeId = TokenTypeId;
		Token->TypeName = TokenType;
		Token->Payload = const_cast<FMethodBindingDescriptor*>(Descriptor.get());
		Token->CloneOwner = std::const_pointer_cast<FMethodBindingDescriptor>(std::move(Descriptor));
		return Token;
	}

	std::shared_ptr<const FMethodBindingDescriptor> ReadMethodBindingToken(const FValue& Value)
	{
		const auto* Token = std::get_if<FHostObjectHandle>(&Value);
		if (!Token || !*Token || (*Token)->TypeId != TokenTypeId || (*Token)->TypeName != TokenType ||
		    !(*Token)->CloneOwner || (*Token)->Payload != (*Token)->CloneOwner.get())
		{
			return {};
		}

		return std::shared_ptr<const FMethodBindingDescriptor>(
		    (*Token)->CloneOwner, static_cast<const FMethodBindingDescriptor*>((*Token)->Payload));
	}
}

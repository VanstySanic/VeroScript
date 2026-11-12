// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

// Deliberately does not link VeroScript: all Core operations use the host vtable.
#include "VeroScriptNativeExtension.h"
#include <cstring>
#include <algorithm>

using namespace VeroScript;
namespace
{
	struct FState
	{
		INativeExtensionHost* Host;
		FHostTypeId Type;
		int Services = 0;
	};
	std::weak_ptr<int> TypeLifetime, GenericLifetime;
	bool Owners(const FNativeCallContext&, FValue& R, std::string&)
	{
		R = std::int32_t{(!TypeLifetime.expired() ? 1 : 0) + (!GenericLifetime.expired() ? 2 : 0)};
		return true;
	}
	void DeleteInt(void* P)
	{
		delete static_cast<int*>(P);
	}
	bool Number(const FNativeCallContext&, FValue& R, std::string&)
	{
		R = std::int32_t{37};
		return true;
	}
	bool Construct(const FNativeCallContext& C, FValue& R, std::string& E)
	{
		auto& S = *static_cast<FState*>(C.UserData);
		auto P = std::make_unique<int>(std::get<std::int32_t>(C.Args[0]));
		auto Object = S.Host->MakeHostObject(S.Type, P.get(), DeleteInt, E);
		if (!Object) return false;
		P.release();
		Object->IsTypeCallback = [](void*, void*, std::string_view N)
		{
			return N == "NativeFixtureObject";
		};
		Object->IsTypeOwner = Object->ModuleOwner;
		R = std::move(Object);
		return true;
	}
	bool Value(const FNativeCallContext& C, FValue& R, std::string&)
	{
		R = static_cast<std::int32_t>(*static_cast<int*>(C.HostThis));
		return true;
	}
	bool Ref(const FNativeCallContext& C, FValue& R, std::string& E)
	{
		if (!C.MutableArg(0))
		{
			E = "expected ref";
			return false;
		}
		*C.MutableArg(0) = std::get<std::int32_t>(C.Args[0]) + std::int32_t{1};
		R = std::int32_t{0};
		return true;
	}
	bool Services(const FNativeCallContext& C, FValue& R, std::string&)
	{
		R = static_cast<std::int32_t>(static_cast<FState*>(C.UserData)->Services);
		return true;
	}
	struct FGenericData
	{
		FState* State;
		std::string Name;
	};
	bool GenericConstruct(const FNativeCallContext& C, FValue& R, std::string& E)
	{
		auto& D = *static_cast<FGenericData*>(C.UserData);
		auto P = std::make_unique<int>(19);
		auto Object = D.State->Host->MakeHostObjectNamed(D.Name, P.get(), DeleteInt, E);
		if (!Object) return false;
		P.release();
		R = std::move(Object);
		return true;
	}
	bool Generic(const std::vector<std::string>& Args, FHostTypeDesc& Out, std::string&, void* U)
	{
		auto D = std::make_shared<FGenericData>();
		D->State = static_cast<FState*>(U);
		D->Name = "NativeFixtureBox<" + Args[0] + ">";
		Out.Kind = EHostTypeKind::Class;
		FHostMemberDesc C;
		C.Kind = EHostMemberKind::Constructor;
		C.Name = "NativeFixtureBox";
		C.ReturnType = ENativeType::HostObject;
		C.Callback = GenericConstruct;
		C.UserData = D.get();
		Out.Members.push_back(C);
		FHostMemberDesc M;
		M.Name = "value";
		M.ReturnType = ENativeType::Int;
		M.Callback = Value;
		M.UserData = D.get();
		Out.Members.push_back(M);
		Out.CallbackLifetimes.push_back({D.get(), D});
		return true;
	}
	class FProcessor final : public ICompileProcessor
	{
	public:
		void Process(const FCompileProcessingContext& C, FCompileProcessingOutput& Out) override
		{
			if (!C.Declaration || C.Declaration->Name != "Generated") return;
			FCompileGenerationOperation O;
			O.Kind = ECompileOperation::AddDeclaration;
			O.Phase = C.Phase;
			O.Scope.ModuleId = C.ModuleId;
			O.Scope.OwnerId = C.Declaration->Id;
			O.Scope.Snapshot = C.Snapshot;
			const auto& Configuration = Host->GetConfiguration();
			const auto Value = Configuration.find("generated-value");
			O.Source = "public: int generated() { return " + (Value == Configuration.end() ? std::string("73") : Value->second) + "; }";
			Host->SubmitCompileOperation(Out, std::move(O));
		}

		explicit FProcessor(INativeExtensionHost* H) : Host(H)
		{
		}

	private:
		INativeExtensionHost* Host;
	};
	void* Service(const FNativeServiceCreateContext&, void* U, std::string&)
	{
		++static_cast<FState*>(U)->Services;
		return new int(41);
	}
	void DestroyService(void* P, void*)
	{
		DeleteInt(P);
	}
	void* VEROSCRIPT_EXTENSION_CALL Create(const void* O, char* Error, std::uint32_t Capacity)
	{
		auto State = std::make_unique<FState>();
		State->Host = const_cast<INativeExtensionHost*>(static_cast<const INativeExtensionHost*>(O));
		auto& H = *State->Host;
		std::string E;
		FHostFunctionDesc F;
		F.Name = "native_fixture_number";
		F.ReturnType = ENativeType::Int;
		if (!H.RegisterFunction(F, Number, State.get(), E))
		{
			goto Failure;
		}

		F.Name = "native_fixture_owners";
		if (!H.RegisterFunction(F, Owners, State.get(), E))
		{
			goto Failure;
		}

		F.Name = "native_fixture_services";
		if (!H.RegisterFunction(F, Services, State.get(), E))
		{
			goto Failure;
		}

		F.Name = "native_fixture_ref";
		F.ReturnType = ENativeType::Void;
		F.ParamTypes = {ENativeType::Int};
		F.ParamFlags = {NativeParamFlagRef};
		if (!H.RegisterFunction(F, Ref, State.get(), E))
		{
			goto Failure;
		}

		{
			FHostTypeDesc T;
			T.Name = "NativeFixtureObject";
			T.Kind = EHostTypeKind::Class;
			T.ModuleOwner = std::make_shared<int>(1);
			TypeLifetime = std::static_pointer_cast<int>(T.ModuleOwner);
			if (!H.RegisterType(T, State->Type, E))
			{
				goto Failure;
			}

			FHostMemberDesc M;
			M.Kind = EHostMemberKind::Constructor;
			M.Name = T.Name;
			M.ReturnType = ENativeType::HostObject;
			M.ParamTypes = {ENativeType::Int};
			M.Callback = Construct;
			M.UserData = State.get();
			if (!H.RegisterMember(State->Type, M, E))
			{
				goto Failure;
			}

			M = {};
			M.Name = "value";
			M.ReturnType = ENativeType::Int;
			M.Callback = Value;
			M.UserData = State.get();
			if (!H.RegisterMember(State->Type, M, E))
			{
				goto Failure;
			}

			FGenericHostTypeTemplateDesc G;
			G.Name = "NativeFixtureBox";
			G.Arity = 1;
			G.Instantiate = Generic;
			G.UserData = State.get();
			G.ModuleOwner = std::make_shared<int>(2);
			GenericLifetime = std::static_pointer_cast<int>(G.ModuleOwner);
			FGenericHostTypeTemplateId Id;
			if (!H.RegisterGenericType(G, Id, E))
			{
				goto Failure;
			}

			FCompileProcessorDesc P;
			P.ProcessorId = "fixture.generate";
			P.Version = "0.1.1";
			P.Phases = CompilePhaseMask(ECompileProcessingPhase::Members);
			P.Operations = CompileOperationMask(ECompileOperation::AddDeclaration);
			P.Subscriptions.push_back(
			    {ECompileProcessingEvent::Declaration, AnnotationTargetMask(EAnnotationTarget::Class)});
			P.Factory = [Host = State->Host]
			{
				return std::make_unique<FProcessor>(Host);
			};
			if (!H.RegisterProcessor(P, E))
			{
				goto Failure;
			}

			FNativeServiceProviderDesc D;
			D.ServiceId = "test";
			D.BackendId = "fixture";
			D.ContractId = "test.service";
			D.ContractVersion = "0.1.1";
			D.Create = Service;
			D.Destroy = DestroyService;
			D.UserData = State.get();
			if (!H.RegisterProvider(D, E))
			{
				goto Failure;
			}
		}
		return State.release();
	Failure:
		if (Capacity)
		{
			const auto N = std::min<std::size_t>(E.size(), Capacity - 1);
			std::memcpy(Error, E.data(), N);
			Error[N] = 0;
		}
		return nullptr;
	}
	void VEROSCRIPT_EXTENSION_CALL Destroy(void* P)
	{
		delete static_cast<FState*>(P);
	}
}
extern "C" VEROSCRIPT_EXTENSION_EXPORT std::uint32_t VEROSCRIPT_EXTENSION_CALL veroscript_extension_query(std::uint32_t V, std::uint32_t Size, void* Out)
{
	if (V != NativeExtensionApiVersion || Size != sizeof(FNativeExtensionDescriptor) || !Out) return 0;
	FNativeExtensionDescriptor D;
	const auto Abi = BuildNativeExtensionAbi();
	if (Abi.size() >= sizeof(D.Abi)) return 0;
	std::memcpy(D.Abi, Abi.c_str(), Abi.size() + 1);
	std::memcpy(D.CoreBuild, VEROSCRIPT_CORE_IMPLEMENTATION_ID, sizeof(VEROSCRIPT_CORE_IMPLEMENTATION_ID));
	std::memcpy(D.Platform, GetHostPlatformInfo().Platform, std::strlen(GetHostPlatformInfo().Platform) + 1);
	std::memcpy(D.Id, "native-fixture", 15);
	std::memcpy(D.Version, "0.1.1", 6);
	D.Roles = 7;
	D.Flags = NativeExtensionParallelRegistrations;
	D.Create = Create;
	D.Destroy = Destroy;
	std::memcpy(Out, &D, sizeof(D));
	return NativeExtensionApiVersion;
}

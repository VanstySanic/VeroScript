// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptTestVm.h"
#include "VeroScriptCompile.h"
#include "VeroScriptBytecodeIO.h"
#include "VeroScriptHostBinding.h"
#include "VeroScriptHostBindingCatalog.h"
#include "VeroScriptMethodKey.h"
#include "VeroScriptModuleMeta.h"
#include "VeroScriptNative.h"
#include "VeroScriptRuntime.h"
#include "VeroScriptVm.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <memory>
#include <sstream>
#include <string>
#include <variant>
#include <vector>

namespace
{
	static_assert(
		static_cast<std::uint8_t>(VeroScript::EHostMemberKind::PropertyGetter) == 3
		&& static_cast<std::uint8_t>(VeroScript::EHostMemberKind::PropertySetter) == 4
		&& static_cast<std::uint8_t>(VeroScript::EHostMemberKind::EnumMember) == 5
		&& static_cast<std::uint8_t>(VeroScript::EHostMemberKind::Field) == 6,
		"existing host-member ABI values must remain stable");
	static_assert(
		static_cast<std::uint8_t>(VeroScript::EHostMemberKind::StaticConstant) == 7,
		"new host-member kinds must be appended");

	int Fail(const char* Message)
	{
		std::cerr << "VeroScriptHostBindingTests: " << Message << '\n';
		return EXIT_FAILURE;
	}

	struct FCounter
	{
		std::int32_t Value = 0;
	};
	std::int32_t GCounterOperatorAddCalls = 0;
	std::int32_t GCounterOperatorEqualCalls = 0;
	std::int32_t GCounterOperatorAddResult = 0;
	bool GCounterOperatorEqualResult = false;
	std::int32_t GCounterStringConstructorCalls = 0;
	bool GStaticConstantContextValid = false;

	bool HostAdd(
		const VeroScript::FNativeCallContext& Context,
		VeroScript::FValue& OutReturn,
		std::string& OutError)
	{
		(void)OutError;
		const auto A = std::get<std::int32_t>(Context.Args[0]);
		const auto B = std::get<std::int32_t>(Context.Args[1]);
		OutReturn = static_cast<std::int32_t>(A + B);
		return true;
	}

	bool ReentrantStampedClone(
		const VeroScript::FNativeCallContext& Context,
		VeroScript::FValue& OutReturn,
		std::string& OutError)
	{
		(void)OutError;
		auto* const Owners =
			static_cast<std::vector<VeroScript::FValue>*>(Context.UserData);
		if (!Owners || Context.ArgCount != 1)
		{
			return false;
		}
		// Reallocate the vector that owns the source FValue while the clone callback
		// is active. Runtime cloning must retain its own stable source handle across
		// this host-reentry-shaped ownership change.
		for (std::int32_t Index = 0; Index < 64; ++Index)
		{
			Owners->push_back(Index);
		}
		auto Cloned = std::make_shared<VeroScript::FHostObject>();
		Cloned->TypeName = "ReentrantValue";
		OutReturn = std::move(Cloned);
		return true;
	}

	bool ThrowingStampedClone(
		const VeroScript::FNativeCallContext&,
		VeroScript::FValue&,
		std::string&)
	{
		throw 7;
	}

	bool ReentrantHostEquals(
		const VeroScript::FNativeCallContext& Context,
		VeroScript::FValue& OutReturn,
		std::string& OutError)
	{
		(void)OutError;
		auto* const Owners =
			static_cast<std::vector<VeroScript::FValue>*>(Context.UserData);
		if (!Owners || Context.ArgCount != 2)
		{
			return false;
		}
		for (std::int32_t Index = 0; Index < 64; ++Index)
		{
			Owners->push_back(Index);
		}
		OutReturn = true;
		return true;
	}

	bool StableHostHash(
		const VeroScript::FNativeCallContext& Context,
		VeroScript::FValue& OutReturn,
		std::string& OutError)
	{
		(void)OutError;
		if (Context.ArgCount != 1)
		{
			return false;
		}
		OutReturn = std::uint64_t{1};
		return true;
	}

	struct FStaticHostBox
	{
		std::int32_t Value = 0;
	};

	struct FHostStaticProbe
	{
		std::int32_t StaticCalls = 0;
		std::int32_t InstanceCalls = 0;
		bool bStaticHostThisWasNull = false;
		bool bStaticReceivedEveryArgument = false;
		bool bInstanceReceivedHostThis = false;
		bool bCorruptTypeAfterStaticCall = false;
	};

	bool StaticHostBoxCtor(
		const VeroScript::FNativeCallContext& Context,
		VeroScript::FValue& OutReturn,
		std::string& OutError)
	{
		if (Context.ArgCount != 1)
		{
			OutError = "StaticHostBox constructor argument mismatch";
			return false;
		}
		auto* const Box = new FStaticHostBox{
			std::get<std::int32_t>(Context.Args[0])};
		OutReturn = VeroScript::MakeHostObject(
			/*TypeId=*/31,
			"StaticHostBox",
			Box,
			[](void* Payload)
			{
				delete static_cast<FStaticHostBox*>(Payload);
			});
		return true;
	}

	bool StaticHostBoxInspect(
		const VeroScript::FNativeCallContext& Context,
		VeroScript::FValue& OutReturn,
		std::string& OutError)
	{
		auto* const Probe = static_cast<FHostStaticProbe*>(Context.UserData);
		if (!Probe)
		{
			OutError = "missing static host probe";
			return false;
		}
		++Probe->StaticCalls;
		Probe->bStaticHostThisWasNull = Context.HostThis == nullptr;
		Probe->bStaticReceivedEveryArgument = Context.ArgCount == 2;
		if (Context.HostThis != nullptr || Context.ArgCount != 2)
		{
			OutError = "static host method received an implicit receiver";
			return false;
		}
		const auto* const Handle =
			std::get_if<VeroScript::FHostObjectHandle>(&Context.Args[0]);
		const auto* const Delta = std::get_if<std::int32_t>(&Context.Args[1]);
		if (!Handle || !*Handle || !Delta)
		{
			OutError = "static host method explicit arguments were not preserved";
			return false;
		}
		auto* const Box = static_cast<FStaticHostBox*>((*Handle)->Payload);
		if (!Box)
		{
			OutError = "static host method received an empty box";
			return false;
		}
		OutReturn = static_cast<std::int32_t>(Box->Value + *Delta);
		if (Probe->bCorruptTypeAfterStaticCall)
		{
			(*Handle)->TypeName = "WrongStaticHostBox";
		}
		return true;
	}

	bool StaticHostBoxAcceptNullable(
		const VeroScript::FNativeCallContext& Context,
		VeroScript::FValue& OutReturn,
		std::string& OutError)
	{
		const auto* const Handle = Context.ArgCount == 1
			? std::get_if<VeroScript::FHostObjectHandle>(&Context.Args[0])
			: nullptr;
		if (Context.HostThis != nullptr || !Handle || *Handle)
		{
			OutError = "nullable static HostObject argument was not preserved";
			return false;
		}
		OutReturn = std::int32_t{1};
		return true;
	}

	bool StaticHostBoxInstanceGet(
		const VeroScript::FNativeCallContext& Context,
		VeroScript::FValue& OutReturn,
		std::string& OutError)
	{
		auto* const Probe = static_cast<FHostStaticProbe*>(Context.UserData);
		auto* const Box = static_cast<FStaticHostBox*>(Context.HostThis);
		if (!Probe || !Box || Context.ArgCount != 1)
		{
			OutError = "instance host method did not receive HostThis";
			return false;
		}
		++Probe->InstanceCalls;
		Probe->bInstanceReceivedHostThis = true;
		OutReturn = Box->Value;
		return true;
	}

	bool HostEchoInt64(
		const VeroScript::FNativeCallContext& Context,
		VeroScript::FValue& OutReturn,
		std::string& OutError)
	{
		const auto* const Value = std::get_if<std::int64_t>(&Context.Args[0]);
		if (!Value || *Value != std::numeric_limits<std::int64_t>::max())
		{
			OutError = "int64 ABI value lost precision";
			return false;
		}
		OutReturn = *Value;
		return true;
	}

	bool HostEchoUInt64(
		const VeroScript::FNativeCallContext& Context,
		VeroScript::FValue& OutReturn,
		std::string& OutError)
	{
		const auto* const Value = std::get_if<std::uint64_t>(&Context.Args[0]);
		if (!Value || *Value != std::numeric_limits<std::uint64_t>::max())
		{
			OutError = "uint64 ABI value lost precision";
			return false;
		}
		OutReturn = *Value;
		return true;
	}

	bool CounterCtor(
		const VeroScript::FNativeCallContext& Context,
		VeroScript::FValue& OutReturn,
		std::string& OutError)
	{
		(void)OutError;
		const auto Initial = std::get<std::int32_t>(Context.Args[0]);
		auto* const Counter = new FCounter{Initial};
		OutReturn = VeroScript::MakeHostObject(
			/*TypeId=*/1,
			"Counter",
			Counter,
			[](void* Payload)
			{
				delete static_cast<FCounter*>(Payload);
			});
		return true;
	}

	bool CounterInc(
		const VeroScript::FNativeCallContext& Context,
		VeroScript::FValue& OutReturn,
		std::string& OutError)
	{
		(void)OutReturn;
		(void)OutError;
		auto* const Counter = static_cast<FCounter*>(Context.HostThis);
		if (!Counter)
		{
			OutError = "null Counter";
			return false;
		}
		++Counter->Value;
		return true;
	}

	bool CounterGet(
		const VeroScript::FNativeCallContext& Context,
		VeroScript::FValue& OutReturn,
		std::string& OutError)
	{
		(void)OutError;
		auto* const Counter = static_cast<FCounter*>(Context.HostThis);
		if (!Counter)
		{
			OutError = "null Counter";
			return false;
		}
		OutReturn = Counter->Value;
		return true;
	}

	bool HostAnswer(
		const VeroScript::FNativeCallContext& Context,
		VeroScript::FValue& OutReturn,
		std::string& OutError)
	{
		if (Context.ArgCount != 0 || Context.HostThis != nullptr)
		{
			OutError = "static constant received arguments or an instance";
			return false;
		}
		GStaticConstantContextValid = true;
		OutReturn = std::int32_t{42};
		return true;
	}

	bool CounterStringCtor(
		const VeroScript::FNativeCallContext& Context,
		VeroScript::FValue& OutReturn,
		std::string& OutError)
	{
		const auto* const Text = Context.ArgCount == 1
			? std::get_if<std::string>(&Context.Args[0])
			: nullptr;
		if (!Text)
		{
			OutError = "Counter string constructor expects string";
			return false;
		}
		++GCounterStringConstructorCalls;
		auto* const Counter = new FCounter{
			static_cast<std::int32_t>(Text->size())};
		OutReturn = VeroScript::MakeHostObject(
			1,
			"Counter",
			Counter,
			[](void* Payload)
			{
				delete static_cast<FCounter*>(Payload);
			});
		return true;
	}

	bool CounterAdd(
		const VeroScript::FNativeCallContext& Context,
		VeroScript::FValue& OutReturn,
		std::string& OutError)
	{
		auto* const Left = static_cast<FCounter*>(Context.HostThis);
		const auto* const RightHandle = Context.ArgCount == 2
			? std::get_if<VeroScript::FHostObjectHandle>(&Context.Args[1])
			: nullptr;
		auto* const Right = RightHandle && *RightHandle
			? static_cast<FCounter*>((*RightHandle)->Payload)
			: nullptr;
		if (!Left || !Right)
		{
			OutError = "Counter operator+ received invalid operands";
			return false;
		}
		++GCounterOperatorAddCalls;
		GCounterOperatorAddResult = Left->Value + Right->Value;
		OutReturn = VeroScript::MakeHostObject(
			1,
			"Counter",
			new FCounter{Left->Value + Right->Value},
			[](void* Payload)
			{
				delete static_cast<FCounter*>(Payload);
			});
		return true;
	}

	bool CounterEqual(
		const VeroScript::FNativeCallContext& Context,
		VeroScript::FValue& OutReturn,
		std::string& OutError)
	{
		auto* const Left = static_cast<FCounter*>(Context.HostThis);
		const auto* const RightHandle = Context.ArgCount == 2
			? std::get_if<VeroScript::FHostObjectHandle>(&Context.Args[1])
			: nullptr;
		auto* const Right = RightHandle && *RightHandle
			? static_cast<FCounter*>((*RightHandle)->Payload)
			: nullptr;
		if (!Left || !Right)
		{
			OutError = "Counter operator== received invalid operands";
			return false;
		}
		++GCounterOperatorEqualCalls;
		GCounterOperatorEqualResult = Left->Value == Right->Value;
		OutReturn = GCounterOperatorEqualResult;
		return true;
	}

	bool GenericBoxCtor(
		const VeroScript::FNativeCallContext& Context,
		VeroScript::FValue& OutReturn,
		std::string& OutError)
	{
		const auto* Value = Context.ArgCount == 1
			? std::get_if<std::int32_t>(&Context.Args[0])
			: nullptr;
		if (!Value)
		{
			OutError = "Box<T> test constructor expects int";
			return false;
		}
		OutReturn = VeroScript::MakeHostObject(
			0,
			{},
			new std::int32_t(*Value),
			[](void* Payload)
			{
				delete static_cast<std::int32_t*>(Payload);
			});
		return true;
	}

	bool GenericBoxGet(
		const VeroScript::FNativeCallContext& Context,
		VeroScript::FValue& OutReturn,
		std::string& OutError)
	{
		auto* Value = static_cast<std::int32_t*>(Context.HostThis);
		const auto* Handle = Context.ArgCount > 0
			? std::get_if<VeroScript::FHostObjectHandle>(&Context.Args[0])
			: nullptr;
		if (!Value || !Handle || !*Handle
			|| (*Handle)->GenericTemplateId == 0
			|| (*Handle)->GenericTypeArguments != std::vector<std::string>{"int"})
		{
			OutError = "Box<T> runtime value lost its generic identity";
			return false;
		}
		OutReturn = *Value;
		return true;
	}

	bool InstantiateGenericBox(
		const std::vector<std::string>& TypeArguments,
		VeroScript::FHostTypeDesc& OutType,
		std::string& OutError,
		void*)
	{
		if (TypeArguments.size() != 1 || TypeArguments[0] != "int")
		{
			OutError = "test Box<T> constrains T to int";
			return false;
		}
		OutType.Kind = VeroScript::EHostTypeKind::Struct;
		OutType.Annotations = {{"HOST_GENERIC", {{"Value", std::string("Box")}}}};
		VeroScript::FHostMemberDesc Ctor;
		Ctor.Kind = VeroScript::EHostMemberKind::Constructor;
		Ctor.ParamTypes = {VeroScript::ENativeType::Int};
		Ctor.ParamAnnotations = {{{"HOST_PARAM", {}}}};
		Ctor.Callback = GenericBoxCtor;
		OutType.Members.push_back(std::move(Ctor));
		VeroScript::FHostMemberDesc Get;
		Get.Kind = VeroScript::EHostMemberKind::Method;
		Get.Name = "get";
		Get.ReturnType = VeroScript::ENativeType::Int;
		Get.Annotations = {{"HOST_MEMBER", {}}};
		Get.Callback = GenericBoxGet;
		OutType.Members.push_back(std::move(Get));
		return true;
	}

	bool CounterGetValue(
		const VeroScript::FNativeCallContext& Context,
		VeroScript::FValue& OutReturn,
		std::string& OutError)
	{
		return CounterGet(Context, OutReturn, OutError);
	}

	bool CounterSetValue(
		const VeroScript::FNativeCallContext& Context,
		VeroScript::FValue& OutReturn,
		std::string& OutError)
	{
		(void)OutReturn;
		(void)OutError;
		auto* const Counter = static_cast<FCounter*>(Context.HostThis);
		if (!Counter)
		{
			OutError = "null Counter";
			return false;
		}
		Counter->Value = std::get<std::int32_t>(Context.Args[1]);
		return true;
	}

	struct FVec2
	{
		std::int32_t X = 0;
		std::int32_t Y = 0;
	};

	bool Vec2Ctor(
		const VeroScript::FNativeCallContext& Context,
		VeroScript::FValue& OutReturn,
		std::string& OutError)
	{
		(void)OutError;
		auto* const Vec = new FVec2{
			std::get<std::int32_t>(Context.Args[0]),
			std::get<std::int32_t>(Context.Args[1])};
		OutReturn = VeroScript::MakeHostObject(
			/*TypeId=*/1,
			"Vec2",
			Vec,
			[](void* Payload)
			{
				delete static_cast<FVec2*>(Payload);
			});
		return true;
	}

	bool Vec2GetX(
		const VeroScript::FNativeCallContext& Context,
		VeroScript::FValue& OutReturn,
		std::string& OutError)
	{
		(void)OutError;
		auto* const Vec = static_cast<FVec2*>(Context.HostThis);
		if (!Vec)
		{
			OutError = "null Vec2";
			return false;
		}
		OutReturn = Vec->X;
		return true;
	}

	bool Vec2SetX(
		const VeroScript::FNativeCallContext& Context,
		VeroScript::FValue& OutReturn,
		std::string& OutError)
	{
		(void)OutReturn;
		(void)OutError;
		auto* const Vec = static_cast<FVec2*>(Context.HostThis);
		if (!Vec)
		{
			OutError = "null Vec2";
			return false;
		}
		Vec->X = std::get<std::int32_t>(Context.Args[1]);
		return true;
	}

	int Vec2CloneCalls = 0;

	bool Vec2Clone(
		const VeroScript::FNativeCallContext& Context,
		VeroScript::FValue& OutReturn,
		std::string& OutError)
	{
		(void)OutError;
		auto* const Src = static_cast<FVec2*>(Context.HostThis);
		if (!Src)
		{
			OutError = "null Vec2";
			return false;
		}
		++Vec2CloneCalls;
		auto* const Copy = new FVec2{*Src};
		OutReturn = VeroScript::MakeHostObject(
			/*TypeId=*/1,
			"Vec2",
			Copy,
			[](void* Payload)
			{
				delete static_cast<FVec2*>(Payload);
			});
		return true;
	}

	bool Vec2Equals(
		const VeroScript::FNativeCallContext& Context,
		VeroScript::FValue& OutReturn,
		std::string& OutError)
	{
		if (Context.ArgCount != 2)
		{
			OutError = "Vec2 equals expects two values";
			return false;
		}
		const auto* LeftHandle =
			std::get_if<VeroScript::FHostObjectHandle>(&Context.Args[0]);
		const auto* RightHandle =
			std::get_if<VeroScript::FHostObjectHandle>(&Context.Args[1]);
		if (!LeftHandle || !*LeftHandle || !RightHandle || !*RightHandle)
		{
			OutError = "Vec2 equals received null";
			return false;
		}
		const auto* Left = static_cast<const FVec2*>((*LeftHandle)->Payload);
		const auto* Right = static_cast<const FVec2*>((*RightHandle)->Payload);
		if (!Left || !Right)
		{
			OutError = "Vec2 equals received empty payload";
			return false;
		}
		OutReturn = Left->X == Right->X && Left->Y == Right->Y;
		return true;
	}

	bool Vec2Hash(
		const VeroScript::FNativeCallContext& Context,
		VeroScript::FValue& OutReturn,
		std::string& OutError)
	{
		const auto* Handle =
			Context.ArgCount == 1
				? std::get_if<VeroScript::FHostObjectHandle>(&Context.Args[0])
				: nullptr;
		const auto* Vec =
			Handle && *Handle ? static_cast<const FVec2*>((*Handle)->Payload) : nullptr;
		if (!Vec)
		{
			OutError = "Vec2 hash received null";
			return false;
		}
		const std::uint64_t X = static_cast<std::uint32_t>(Vec->X);
		const std::uint64_t Y = static_cast<std::uint32_t>(Vec->Y);
		OutReturn = (X << 32u) ^ Y;
		return true;
	}

	bool Vec2CloneFail(
		const VeroScript::FNativeCallContext& Context,
		VeroScript::FValue& OutReturn,
		std::string& OutError)
	{
		(void)Context;
		(void)OutReturn;
		OutError = "Vec2 @clone forced failure";
		return false;
	}

	VeroScript::FHostObjectHandle MakeRawVec2(const std::int32_t X, const std::int32_t Y)
	{
		return VeroScript::MakeHostObject(
			/*TypeId=*/1,
			"Vec2",
			new FVec2{X, Y},
			[](void* Payload)
			{
				delete static_cast<FVec2*>(Payload);
			});
	}

	bool NativeVec2Map(
		const VeroScript::FNativeCallContext&,
		VeroScript::FValue& OutReturn,
		std::string&)
	{
		auto Result = std::make_shared<VeroScript::FMap>();
		Result->TypeName = "map<Vec2,int>";
		Result->KeyTypeName = "Vec2";
		Result->ValueTypeName = "int";
		Result->HostEntries.emplace(MakeRawVec2(7, 8), std::int32_t{31});
		OutReturn = std::move(Result);
		return true;
	}

	bool NativeVec2OrderedMap(
		const VeroScript::FNativeCallContext&,
		VeroScript::FValue& OutReturn,
		std::string&)
	{
		auto Result = std::make_shared<VeroScript::FOrderedMap>();
		Result->TypeName = "ordered_map<Vec2,int>";
		Result->KeyTypeName = "Vec2";
		Result->ValueTypeName = "int";
		VeroScript::FHostObjectHandle Key = MakeRawVec2(9, 10);
		Result->HostKeyOrder.push_back(Key);
		Result->HostEntries.emplace(std::move(Key), std::int32_t{37});
		OutReturn = std::move(Result);
		return true;
	}

	bool NativeVec2Set(
		const VeroScript::FNativeCallContext&,
		VeroScript::FValue& OutReturn,
		std::string&)
	{
		auto Result = std::make_shared<VeroScript::FSet>();
		Result->TypeName = "set<Vec2>";
		Result->ElementTypeName = "Vec2";
		Result->HostElements.emplace(MakeRawVec2(11, 12));
		OutReturn = std::move(Result);
		return true;
	}

	bool ExpectEq(
		const VeroScript::FNativeCallContext& Context,
		VeroScript::FValue& OutReturn,
		std::string& OutError)
	{
		(void)OutReturn;
		const auto Actual = std::get<std::int32_t>(Context.Args[0]);
		const auto Expected = std::get<std::int32_t>(Context.Args[1]);
		if (Actual != Expected)
		{
			OutError = "expect_eq failed: got " + std::to_string(Actual)
			+ " expected " + std::to_string(Expected);
			return false;
		}
		return true;
	}

	struct FActor
	{
		std::int32_t Value = 0;
		std::string Name = "host";
		std::int32_t Tag = 7;
	};

	int ActorSubclassCalls = 0;

	bool ActorSubclassFactory(
		const VeroScript::FNativeCallContext& Context,
		VeroScript::FValue& OutReturn,
		std::string& OutError)
	{
		(void)OutError;
		++ActorSubclassCalls;
		if (Context.ArgCount < 1 || !std::holds_alternative<std::string>(Context.Args[0]))
		{
			OutError = "subclass factory expects script type name";
			return false;
		}
		auto* const Actor = new FActor{42};
		OutReturn = VeroScript::MakeHostObject(
			/*TypeId=*/1,
			"AActor",
			Actor,
			[](void* Payload)
			{
				delete static_cast<FActor*>(Payload);
			});
		return true;
	}

	bool ActorSubclassFactoryNull(
		const VeroScript::FNativeCallContext& Context,
		VeroScript::FValue& OutReturn,
		std::string& OutError)
	{
		(void)Context;
		(void)OutReturn;
		OutError = "factory refused";
		return false;
	}

	bool ActorGetValue(
		const VeroScript::FNativeCallContext& Context,
		VeroScript::FValue& OutReturn,
		std::string& OutError)
	{
		(void)OutError;
		auto* const Actor = static_cast<FActor*>(Context.HostThis);
		if (!Actor)
		{
			OutError = "null AActor";
			return false;
		}
		OutReturn = Actor->Value;
		return true;
	}

	bool ActorGetName(
		const VeroScript::FNativeCallContext& Context,
		VeroScript::FValue& OutReturn,
		std::string& OutError)
	{
		auto* const Actor = static_cast<FActor*>(Context.HostThis);
		if (!Actor)
		{
			OutError = "null AActor";
			return false;
		}
		OutReturn = Actor->Name;
		return true;
	}

	bool ActorGetTag(
		const VeroScript::FNativeCallContext& Context,
		VeroScript::FValue& OutReturn,
		std::string& OutError)
	{
		auto* const Actor = static_cast<FActor*>(Context.HostThis);
		if (!Actor)
		{
			OutError = "null AActor";
			return false;
		}
		OutReturn = Actor->Tag;
		return true;
	}

	bool ActorSetTag(
		const VeroScript::FNativeCallContext& Context,
		VeroScript::FValue& OutReturn,
		std::string& OutError)
	{
		(void)OutReturn;
		auto* const Actor = static_cast<FActor*>(Context.HostThis);
		if (!Actor)
		{
			OutError = "null AActor";
			return false;
		}
		if (Context.ArgCount < 2 || !std::holds_alternative<std::int32_t>(Context.Args[1]))
		{
			OutError = "Tag setter expects int";
			return false;
		}
		Actor->Tag = std::get<std::int32_t>(Context.Args[1]);
		return true;
	}

	bool ExpectStr(
		const VeroScript::FNativeCallContext& Context,
		VeroScript::FValue& OutReturn,
		std::string& OutError)
	{
		(void)OutReturn;
		const auto& Actual = std::get<std::string>(Context.Args[0]);
		const auto& Expected = std::get<std::string>(Context.Args[1]);
		if (Actual != Expected)
		{
			OutError = "expect_str failed: got " + Actual + " expected " + Expected;
			return false;
		}
		return true;
	}

	int Vec2SubclassCalls = 0;

	bool Vec2SubclassFactory(
		const VeroScript::FNativeCallContext& Context,
		VeroScript::FValue& OutReturn,
		std::string& OutError)
	{
		(void)OutError;
		++Vec2SubclassCalls;
		if (Context.ArgCount < 1 || !std::holds_alternative<std::string>(Context.Args[0]))
		{
			OutError = "subclass factory expects script type name";
			return false;
		}
		auto* const Vec = new FVec2{5, 9};
		OutReturn = VeroScript::MakeHostObject(
			/*TypeId=*/1,
			"Vec2",
			Vec,
			[](void* Payload)
			{
				delete static_cast<FVec2*>(Payload);
			});
		return true;
	}
}

int main()
{
	{
		VeroScript::FHostBindingRegistry EmptyCatalog;
		for (const std::string Type : {"int[2][3]", "ref int[2][3]", "fn(ref int[2][3])->int",
			"array<int[2]>", "map<int,int[2]>", "pair<int[2],int[3]>",
			"array<fn(ref int[2][3])->int>", "array<int[2]>[3][4]"})
		{
			std::string Resolved;
			std::string Error;
			if (!VeroScript::ResolveHostRuntimeTypeName(EmptyCatalog, Type, Resolved, Error)
				|| Resolved != Type)
			{
				return Fail("empty host catalog must preserve native array dimension order");
			}
		}
		std::string Error;
		if (!EmptyCatalog.RegisterTypeAlias("Tiny", {}, "int8", Error)
			|| !EmptyCatalog.RegisterTypeAlias("Row", {}, "Tiny[2]", Error))
		{
			return Fail("failed to register nested array aliases");
		}
		for (const auto& Case : {std::pair{"array<Row>", "array<char[2]>"},
			std::pair{"map<int,Row>", "map<int,char[2]>"},
			std::pair{"pair<Row,Tiny[3]>", "pair<char[2],char[3]>"},
			std::pair{"Row[3]", "char[3][2]"},
			std::pair{"array<Row>[3][4]", "array<char[2]>[3][4]"},
			std::pair{"array<fn(ref Row)->Tiny>[3]", "array<fn(ref char[2])->char>[3]"}})
		{
			std::string Resolved, Again;
			if (!VeroScript::ResolveHostRuntimeTypeName(EmptyCatalog, Case.first, Resolved, Error)
				|| Resolved != Case.second
				|| !VeroScript::ResolveHostRuntimeTypeName(EmptyCatalog, Resolved, Again, Error)
				|| Again != Resolved)
			{
				return Fail("nested array aliases must preserve dimension ownership and be idempotent");
			}
		}
	}
	// Ordered domain suites share the callback fixtures declared above.
	#include "Suites/VeroScriptHostBinding/01_catalog_and_registry.inl"
	#include "Suites/VeroScriptHostBinding/02_host_values_and_inheritance.inl"
	#include "Suites/VeroScriptHostBinding/03_containers_and_references.inl"
	#include "Suites/VeroScriptHostBinding/04_metadata_and_enums.inl"
	#include "Suites/VeroScriptHostBinding/05_generics_and_serialization.inl"
}

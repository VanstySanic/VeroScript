// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptTestVm.h"
#include "VeroScriptValueRoot.h"
#include "VeroScriptVm.h"
#include "../VeroScriptValueGraphInternal.h"
#include <algorithm>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <thread>

using namespace VeroScript;
#define CHECK(X) \
	do \
	{ \
		if (!(X)) \
		{ \
			throw std::runtime_error(#X); \
		} \
	} \
	while (false)

int main()
{
	try
	{
		{
			auto Owner = std::make_shared<FValueRootOwner>();
			std::string Error;
			FValueRoot Root, Ordinary;
			CHECK(Owner->RetainForMigration(std::int32_t{7}, Root, Error));
			CHECK(!Owner->HasExternalRoots(false) && Owner->HasExternalRoots());
			CHECK(Owner->Retain(std::int32_t{1}, Ordinary, Error));
			std::vector<FValueRoot> Roots;
			std::vector<FValue> Values;
			CHECK(!Owner->PrepareMigration(Roots, Values, Error) && Root.IsValid() && Roots.empty());
			Ordinary.Reset();
			CHECK(Owner->Drain([](auto& Retired, const auto&)
			{
				Retired.clear();
				return true;
			}, Error));
			CHECK(Owner->PrepareMigration(Roots, Values, Error) && !Root.Read() && Roots.size() == 1);
			std::vector<FValueRoot> Missing;
			CHECK(!Owner->CompleteMigration(Missing, nullptr, Error) && !Root.Read());
			auto Duplicate = Roots;
			Duplicate.push_back(Roots.front());
			CHECK(!Owner->CompleteMigration(Duplicate, nullptr, Error) && !Root.Read());
			Duplicate.clear();
			Values.push_back(std::int32_t{0});
			CHECK(!Owner->CompleteMigration(Roots, &Values, Error) && !Root.Read());
			Values.pop_back();
			Values[0] = std::int32_t{9};
			CHECK(Owner->CompleteMigration(Roots, &Values, Error) && std::get<std::int32_t>(*Root.Read()) == 9);
			Roots.clear();
			Values.clear();
			CHECK(Owner->PrepareMigration(Roots, Values, Error));
			std::thread Retire([&]
			{
				Root.Reset();
			});
			Retire.join();
			CHECK(Owner->CompleteMigration(Roots, nullptr, Error) && Roots[0].IsValid());
			Roots.clear();
			std::size_t RetiredCount = 0;
			CHECK(Owner->Drain([&](auto& Retired, const auto& Keep)
			{
				CHECK(Keep.empty());
				RetiredCount += Retired.size();
				Retired.clear();
				return true;
			}, Error) && RetiredCount == 1);
		}
		// A cleanup snapshot may protect the same graph through many callbacks.
		// Shared traversal state must visit its descendants once across all roots.
		auto Shared = std::make_shared<FDynamicArray>();
		Shared->Elements.assign(100, FValue{std::int32_t{3}});
		auto Parent = std::make_shared<FDynamicArray>();
		Parent->Elements = {Shared, Shared};
		std::unordered_set<const void*> Seen;
		int Scalars = 0;
		const auto Visit = [&](const FValue& Value, const void*)
		{
			if (std::holds_alternative<std::int32_t>(Value))
			{
				++Scalars;
			}
			return true;
		};
		CHECK(ValueGraphInternal::Visit(Parent, Visit, Seen));
		CHECK(ValueGraphInternal::Visit(Shared, Visit, Seen));
		CHECK(Scalars == 100 && Seen.size() == 2);
		// A later snapshot must see mutations; traversal state is not persistent.
		Shared->Elements.push_back(std::int32_t{4});
		Seen.clear();
		Scalars = 0;
		CHECK(ValueGraphInternal::Visit(Shared, Visit, Seen) && Scalars == 101);
		// Retiring a disjoint tree releases it, while a shared descendant remains
		// intact through another live root. Cycles still require ordinary detachment.
		std::ostringstream Output;
		VeroScriptVm Vm(Output);
		VeroScript::Test::Configure(Vm);
		FBytecodeModule Module;
		auto KeepArray = std::make_shared<FDynamicArray>();
		KeepArray->Elements = {std::int32_t{42}};
		std::vector<FValue> Keep{KeepArray};
		auto Retired = std::make_shared<FDynamicArray>();
		Retired->Elements = {std::make_shared<FDynamicArray>()};
		std::weak_ptr<FDynamicArray> Child = std::get<FDynamicArrayHandle>(Retired->Elements.front());
		std::vector<FValue> RetiredValues{std::move(Retired)};
		CHECK(Vm.DiscardExternalValues(Module, RetiredValues, Keep) && Child.expired());
		CHECK(KeepArray->Elements.size() == 1 && std::get<std::int32_t>(KeepArray->Elements.front()) == 42);
		Retired = std::make_shared<FDynamicArray>();
		Retired->Elements = {KeepArray, Retired};
		std::weak_ptr<FDynamicArray> Cycle = Retired;
		RetiredValues = {std::move(Retired)};
		CHECK(Vm.DiscardExternalValues(Module, RetiredValues, Keep) && Cycle.expired());
		CHECK(KeepArray->Elements.size() == 1 && std::get<std::int32_t>(KeepArray->Elements.front()) == 42);
		auto Owner = std::make_shared<FValueRootOwner>();
		std::string Error;
		FValueRoot External, Scoped, Copy;
		CHECK(Owner->Retain(std::int32_t{7}, External, Error));
		auto Marker = std::make_shared<int>(1);
		std::weak_ptr<int> Probe = Marker;
		auto Object = std::make_shared<FHostObject>();
		Object->ModuleOwner = Marker;
		CHECK(Owner->RetainForGeneration(Object, Scoped, Error));
		Copy = Scoped;
		Object.reset();
		Marker.reset();
		CHECK(Owner->HasLiveRoots() && Owner->HasExternalRoots() && !Probe.expired());
		std::vector<FValue> Snapshot;
		bool ForeignInvalidated = true;
		std::thread Foreign(
		    [&]
		{
			std::string Failure;
			Snapshot = Owner->SnapshotLiveValues();
			ForeignInvalidated = Owner->InvalidateGeneration(Failure);
		});
		Foreign.join();
		CHECK(!ForeignInvalidated && Scoped.IsValid() && External.IsValid());
		CHECK(Snapshot.size() == 2);
		CHECK(std::any_of(Snapshot.begin(), Snapshot.end(), [](const FValue& Value)
		{
			const auto* Number = std::get_if<std::int32_t>(&Value);
			return Number && *Number == 7;
		}));
		CHECK(std::any_of(Snapshot.begin(), Snapshot.end(), [&](const FValue& Value)
		{
			const auto* Host = std::get_if<FHostObjectHandle>(&Value);
			return Host && *Host && (*Host)->ModuleOwner == Probe.lock();
		}));
		CHECK(Owner->InvalidateGeneration(Error));
		CHECK(!Scoped.IsValid() && !Copy.IsValid() && External.IsValid() && !Probe.expired());
		// Invalidated generation roots remain queued for cleanup, but may no
		// longer protect a graph during another selective cleanup operation.
		Snapshot = Owner->SnapshotLiveValues();
		CHECK(Snapshot.size() == 1 && std::get<std::int32_t>(Snapshot.front()) == 7);
		const auto Thread = std::this_thread::get_id();
		std::size_t Cleaned = 0;
		auto Cleanup = [&](auto& Values, const auto& Keep)
		{
			CHECK(std::this_thread::get_id() == Thread);
			Cleaned += Values.size();
			CHECK(Keep.size() == 1 && std::get<std::int32_t>(Keep.front()) == 7);
			Values.clear();
			return true;
		};
		CHECK(Owner->Drain(Cleanup, Error) && Probe.expired() && Cleaned == 1 && External.IsValid());
		CHECK(Owner->InvalidateGeneration(Error) && Owner->Drain(Cleanup, Error) && Cleaned == 1);
		External.Reset();
		CHECK(Owner->SnapshotLiveValues().empty());
		CHECK(Owner->Drain(
		    [](auto& Values, const auto& Keep)
		{
			CHECK(Keep.empty());
			Values.clear();
			return true;
		},
		    Error));
		CHECK(!Owner->HasExternalRoots() && !Owner->HasLiveRoots());
		FValueRoot Reservation;
		CHECK(Owner->Reserve(Reservation, Error) && Owner->HasExternalRoots());
		CHECK(Owner->SnapshotLiveValues().empty());
		Reservation.Reset();
		CHECK(!Owner->HasExternalRoots());
		CHECK(Owner->RetainForGeneration(std::int32_t{11}, Scoped, Error));
		CHECK(Owner->InvalidateGeneration(Error));
		CHECK(!Owner->Drain(
		    [](auto&, const auto&)
		{
			return false;
		},
		    Error));
		CHECK(!Scoped.IsValid() && !Error.empty());
		CHECK(Owner->Drain(
		    [](auto& Values, const auto& Keep)
		{
			CHECK(Values.size() == 1 && std::get<std::int32_t>(Values.front()) == 11 && Keep.empty());
			Values.clear();
			return true;
		},
		    Error));
		CHECK(!Scoped.IsValid() && !Owner->HasLiveRoots());
		CHECK(Owner->RetainForGeneration(std::int32_t{9}, Scoped, Error));
		CHECK(Scoped.IsValid() && Owner->HasLiveRoots() && !Owner->HasExternalRoots());
		std::thread Retire(
		    [Root = std::move(Scoped)]() mutable
		{
			Root.Reset();
		});
		Retire.join();
		CHECK(Owner->Drain(
		    [](auto& Values, const auto& Keep)
		{
			CHECK(Values.size() == 1 && Keep.empty());
			Values.clear();
			return true;
		},
		    Error));
		CHECK(!Owner->HasLiveRoots());
		// A native safe point must not consume an ordinary host borrow. Its
		// enclosing host call still needs that root for language cleanup.
		CHECK(Owner->Retain(std::int32_t{31}, External, Error));
		External.Reset();
		CHECK(Owner->RetainForGeneration(std::int32_t{41}, Scoped, Error));
		Scoped.Reset();
		CHECK(Owner->DrainGeneration(
		    [&](auto& Values, const auto& Keep)
		{
			CHECK(Values.size() == 1 && std::get<std::int32_t>(Values.front()) == 41);
			CHECK(Keep.size() == 1 && std::get<std::int32_t>(Keep.front()) == 31);
			CHECK(Owner->RetainForGeneration(std::int32_t{43}, Scoped, Error));
			Scoped.Reset(); // Retirement during cleanup must survive its epoch.
			Values.clear();
			return true;
		},
		    Error));
		CHECK(Owner->DrainGeneration(
		    [](auto& Values, const auto& Keep)
		{
			CHECK(Values.size() == 1 && std::get<std::int32_t>(Values.front()) == 43);
			CHECK(Keep.size() == 1 && std::get<std::int32_t>(Keep.front()) == 31);
			Values.clear();
			return true;
		},
		    Error));
		CHECK(Owner->DrainGeneration(
		    [](auto&, const auto&) -> bool
		{
			throw std::runtime_error("no generation roots are pending");
		},
		    Error));
		CHECK(Owner->Drain(
		    [](auto& Values, const auto& Keep)
		{
			CHECK(Values.size() == 1 && std::get<std::int32_t>(Values.front()) == 31 && Keep.empty());
			Values.clear();
			return true;
		},
		    Error));
		CHECK(Owner->Close(
		    [](auto& Values, const auto&)
		{
			Values.clear();
			return true;
		},
		    Error));
		CHECK(!Owner->RetainForGeneration(std::int32_t{1}, Scoped, Error) && !Copy.IsValid());
		std::cout << "Generation roots, owner cleanup, export protection and cross-thread retirement passed\n";
		return 0;
	}
	catch (const std::exception& Error)
	{
		std::cerr << Error.what() << '\n';
		return 1;
	}
}

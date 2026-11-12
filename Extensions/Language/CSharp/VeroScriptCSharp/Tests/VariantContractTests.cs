// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

using VeroScript;

internal static class VariantContractTests
{
	internal static void Run()
	{
		using var session = new VeroScriptSession();
		using var payload = new VeroScriptValue();
		payload.Set(42);
		using var value = session.CreateVariant("int", payload);
		if (value.Kind != VeroScriptValueKind.Variant || !value.VariantHasValue || value.VariantTypeName != "int")
		{
			throw new InvalidOperationException("variant identity");
		}

		using var result = value.GetVariantValue();
		if (result.GetInt32() != 42)
		{
			throw new InvalidOperationException("variant payload");
		}

		using var copy = value.Clone();
		value.SetVariantEmpty();
		if (value.VariantHasValue || !copy.VariantHasValue)
		{
			throw new InvalidOperationException("variant copy slots");
		}

		foreach (string name in new[]
		{
			"variant",
			"std::variant",
			"std.variant",
			" variant "
		}

		)
		{
			using var aliased = session.CreateVariant(name, copy);
			using var extracted = aliased.GetVariantValue();
			if (aliased.VariantTypeName != "int" || extracted.GetInt32() != 42)
			{
				throw new InvalidOperationException("canonical flat variant copy");
			}
		}

		session.RegisterFunction("echo_variant", VeroScriptNativeType.Variant, [new(VeroScriptNativeType.Variant)], (context, result) => result.Set(context.GetArgument(0)));
		if (!session.LoadSource("class Item{}; variant make(){return Item();} variant answer(){return echo_variant(42);} int read(variant v){return cast<int>(v);}").Success)
		{
			throw new InvalidOperationException("load variant callback fixture");
		}

		var(readValue, readCall) = session.CallFunction("read", copy);
		using var readResult = readValue;
		if (!readCall.Success || readResult.GetInt32() != 42)
		{
			throw new InvalidOperationException("host variant argument resolution");
		}

		var(returned, call) = session.CallFunction("answer");
		using var returnedBox = returned;
		using var returnedValue = returnedBox.GetVariantValue();
		if (!call.Success || returnedValue.GetInt32() != 42)
		{
			throw new InvalidOperationException("native variant signature");
		}

		var(owned, makeCall) = session.CallFunction("make");
		using var ownedBox = owned;
		if (!makeCall.Success || !ownedBox.VariantHasValue)
		{
			throw new InvalidOperationException("export module box");
		}

		session.Dispose();
		if (ownedBox.Kind != VeroScriptValueKind.Invalid)
		{
			throw new InvalidOperationException("closed module box invalidation");
		}

		using var detached = copy.GetVariantValue();
		if (detached.GetInt32() != 42)
		{
			throw new InvalidOperationException("independent scalar box");
		}
	}
}

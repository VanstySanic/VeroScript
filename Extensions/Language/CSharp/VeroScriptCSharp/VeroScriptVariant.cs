// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

using VeroScript.Interop;

namespace VeroScript;
public unsafe partial class VeroScriptValue
{
	/// <summary>Replaces this value with an empty dynamic value box.</summary>
	public void SetVariantEmpty()
	{
		using var lifetime = new NativeLifetime(this);
		ThrowIfFailed(NativeMethods.vs_interop_value_set_variant_empty(Handle), "set empty variant");
	}

	/// <summary>Snapshots a payload using its canonical type in the given session.</summary>
	public void SetVariant(VeroScriptSession session, string typeName, VeroScriptValue payload)
	{
		ArgumentNullException.ThrowIfNull(session);
		ArgumentNullException.ThrowIfNull(payload);
		session.BoxVariant(this, typeName, payload);
	}

	public bool VariantHasValue
	{
		get
		{
			using var lifetime = new NativeLifetime(this);
			byte present;
			ThrowIfFailed(NativeMethods.vs_interop_variant_has_value(Handle, &present), "query variant");
			return present != 0;
		}
	}

	public string VariantTypeName
	{
		get
		{
			using var lifetime = new NativeLifetime(this);
			NativeStringView name;
			ThrowIfFailed(NativeMethods.vs_interop_variant_get_type_name(Handle, &name), "get variant payload type");
			return Utf8.Copy(name);
		}
	}

	public VeroScriptValue GetVariantValue()
	{
		using var lifetime = new NativeLifetime(this);
		var output = new VeroScriptValue();
		try
		{
			ThrowIfFailed(NativeMethods.vs_interop_variant_get(Handle, output.Handle), "extract variant payload");
			return output;
		}
		catch
		{
			output.Dispose();
			throw;
		}
	}
}

public sealed unsafe partial class VeroScriptSession
{
	internal void BoxVariant(VeroScriptValue target, string typeName, VeroScriptValue payload)
	{
		using var lifetime = new NativeLifetime(this, target, payload);
		CheckOwner();
		using var type = new Utf8(typeName);
		CheckStatus(NativeMethods.vs_interop_value_set_variant(Handle, target.Handle, type.View, payload.Handle), "box variant");
	}

	public VeroScriptValue CreateVariant(string typeName, VeroScriptValue payload)
	{
		ArgumentNullException.ThrowIfNull(payload);
		var value = new VeroScriptValue();
		try
		{
			BoxVariant(value, typeName, payload);
			return value;
		}
		catch
		{
			value.Dispose();
			throw;
		}
	}
}

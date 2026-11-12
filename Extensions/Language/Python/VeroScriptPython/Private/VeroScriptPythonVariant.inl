// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

	PyObject* ValueVariantEmpty(PyObject*, PyObject*)
	{
		vs_interop_value* Out = nullptr;
		auto Status = vs_interop_value_create(&Out);
		if (Status == VS_INTEROP_STATUS_OK)
		{
			Status = vs_interop_value_set_variant_empty(Out);
		}
		if (Status != VS_INTEROP_STATUS_OK)
		{
			vs_interop_value_destroy(Out);
			return RaiseStatus(nullptr, Status, "create variant");
		}
		return NewValueTakingOwnership(Out);
	}
	PyObject* ValueVariantHasValue(FPyValue* Self, PyObject*)
	{
		uint8_t Present = 0;
		const auto Status = vs_interop_variant_has_value(Self->Handle, &Present);
		if (Status != VS_INTEROP_STATUS_OK) return RaiseStatus(nullptr, Status, "query variant");
		return PyBool_FromLong(Present != 0);
	}
	PyObject* ValueVariantSetEmpty(FPyValue* Self, PyObject*)
	{
		const auto Status = vs_interop_value_set_variant_empty(Self->Handle);
		if (Status != VS_INTEROP_STATUS_OK) return RaiseStatus(nullptr, Status, "set empty variant");
		Py_RETURN_NONE;
	}
	PyObject* ValueVariantAssign(FPyValue* Self, PyObject* Other)
	{
		if (!PyObject_TypeCheck(Other, &GValueType) || vs_interop_value_get_kind(reinterpret_cast<FPyValue*>(Other)->Handle) != VS_INTEROP_VALUE_VARIANT)
		{
			PyErr_SetString(PyExc_TypeError, "assign_variant requires a variant Value");
			return nullptr;
		}
		const auto Status = vs_interop_value_assign(Self->Handle, reinterpret_cast<FPyValue*>(Other)->Handle);
		if (Status != VS_INTEROP_STATUS_OK) return RaiseStatus(nullptr, Status, "assign variant");
		Py_RETURN_NONE;
	}
	PyObject* ValueVariantTypeName(FPyValue* Self, PyObject*)
	{
		vs_interop_string_view Type{};
		const auto Status = vs_interop_variant_get_type_name(Self->Handle, &Type);
		if (Status != VS_INTEROP_STATUS_OK) return RaiseStatus(nullptr, Status, "query variant type");
		return UnicodeFromView(Type);
	}
	PyObject* ValueVariantGet(FPyValue* Self, PyObject*)
	{
		vs_interop_value* Out = nullptr;
		auto Status = vs_interop_value_create(&Out);
		if (Status == VS_INTEROP_STATUS_OK)
		{
			Status = vs_interop_variant_get(Self->Handle, Out);
		}
		if (Status != VS_INTEROP_STATUS_OK)
		{
			vs_interop_value_destroy(Out);
			return RaiseStatus(nullptr, Status, "extract variant");
		}
		return NewValueTakingOwnership(Out);
	}
	PyObject* SessionVariant(FPySession* Self, PyObject* Args)
	{
		const char* Type = nullptr;
		Py_ssize_t Size = 0;
		PyObject* Payload = nullptr;
		if (!PyArg_ParseTuple(Args, "s#O:variant", &Type, &Size, &Payload) || !CheckSession(Self)) return nullptr;
		if (!PyObject_TypeCheck(Payload, &GValueType))
		{
			PyErr_SetString(PyExc_TypeError, "variant payload must be a Value");
			return nullptr;
		}
		const FCallbackActivity Activity(Self->Callbacks);
		const FCallbackErrorScope ErrorScope(Self);
		vs_interop_value* Out = nullptr;
		auto Status = vs_interop_value_create(&Out);
		if (Status == VS_INTEROP_STATUS_OK)
		{
			Status = vs_interop_value_set_variant(Self->Handle, Out, View(Type, Size), reinterpret_cast<FPyValue*>(Payload)->Handle);
		}
		if (RaiseCallbackError(Self))
		{
			vs_interop_value_destroy(Out);
			return nullptr;
		}
		if (Status != VS_INTEROP_STATUS_OK)
		{
			vs_interop_value_destroy(Out);
			return RaiseStatus(Self, Status, "box variant");
		}
		return NewValueTakingOwnership(Out);
	}

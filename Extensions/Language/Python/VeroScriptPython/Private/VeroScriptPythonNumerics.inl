// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

	// Included inside the binding implementation namespace after session helpers.

	PyObject* NumericDispatch(FPySession* Self, PyObject* Args)
	{
		if (!CheckInterpreter()) return nullptr;
		if (PyTuple_GET_SIZE(Args) == 0 || !PyUnicode_Check(PyTuple_GET_ITEM(Args, 0)))
		{
			PyErr_SetString(PyExc_TypeError, "numeric operation required");
			return nullptr;
		}
		const char* Operation = PyUnicode_AsUTF8(PyTuple_GET_ITEM(Args, 0));
		if (!Operation) return nullptr;
		const std::string_view Op(Operation);
		const char* Text = nullptr;
		Py_ssize_t TextSize = 0;
		PyObject* KindObject = nullptr;
		PyObject* ValueObject = nullptr;
		std::uint32_t Kind = 0;
		vs_interop_native_type Target = VS_INTEROP_NATIVE_VOID;
		int ExactFloat = 0;
		vs_interop_numeric_limits Limits{};
		Limits.struct_size = sizeof(Limits);
		if (Op == "limits")
		{
			PyObject* Fields[5]{};
			if (!PyArg_ParseTuple(Args, "sOOOOO:numeric_limits", &Operation,
			        &Fields[0], &Fields[1], &Fields[2], &Fields[3], &Fields[4]))
			{
				return nullptr;
			}
			std::uint64_t Parsed[5]{};
			for (std::size_t I = 0; I < 5; ++I)
			{
				Parsed[I] = PyLong_AsUnsignedLongLong(Fields[I]);
				if (PyErr_Occurred()) return nullptr;
			}
			Limits.max_value_bits = Parsed[0];
			Limits.max_input_bytes = Parsed[1];
			Limits.max_output_bytes = Parsed[2];
			Limits.max_scratch_bytes = Parsed[3];
			Limits.max_work_units = Parsed[4];
		}
		else if (Op == "parse")
		{
			if (!PyArg_ParseTuple(Args, "ss#O:numeric_parse", &Operation,
			        &Text, &TextSize, &KindObject))
			{
				return nullptr;
			}
			const auto Parsed = PyLong_AsUnsignedLongLong(KindObject);
			if (PyErr_Occurred()) return nullptr;
			if (Parsed != 1 && Parsed != 2)
			{
				PyErr_SetString(PyExc_ValueError, "numeric kind must be BIGINT or RATIONAL");
				return nullptr;
			}
			Kind = static_cast<std::uint32_t>(Parsed);
		}
		else if (Op == "text")
		{
			if (!PyArg_ParseTuple(Args, "sO!:numeric_to_text", &Operation,
			        &GValueType, &ValueObject))
			{
				return nullptr;
			}
		}
		else if (Op == "convert")
		{
			if (!PyArg_ParseTuple(Args, "sO!s#p:numeric_convert", &Operation,
			        &GValueType, &ValueObject, &Text, &TextSize, &ExactFloat))
			{
				return nullptr;
			}
			if (!ParseNativeTypeName(std::string_view(Text, static_cast<std::size_t>(TextSize)), &Target) || Target == VS_INTEROP_NATIVE_VOID || Target == VS_INTEROP_NATIVE_BOOL || Target == VS_INTEROP_NATIVE_STRING || Target == VS_INTEROP_NATIVE_VARIANT)
			{
				PyErr_SetString(PyExc_ValueError, "numeric target must be an integer, float or double type");
				return nullptr;
			}
		}
		else
		{
			PyErr_SetString(PyExc_ValueError, "unknown numeric operation");
			return nullptr;
		}

		// Python truth conversion above can close the session. Capture handles
		// only after conversions, then protect callbacks while releasing the GIL.
		if (Self && !CheckSession(Self)) return nullptr;
		if (!Self && (!ActiveMemoryCall || Op == "limits"))
		{
			PyErr_SetString(GError, "numeric services require an active native callback; limits require a session");
			return nullptr;
		}
		auto* Session = Self ? Self->Handle : nullptr;
		const auto* Call = Self ? nullptr : ActiveMemoryCall;
		using FOwnedNumericValue = std::unique_ptr<vs_interop_value, decltype(&vs_interop_value_destroy)>;
		FOwnedNumericValue Output(nullptr, &vs_interop_value_destroy);
		FOwnedNumericValue Input(nullptr, &vs_interop_value_destroy);
		vs_interop_status Status = VS_INTEROP_STATUS_OK;
		if (Op != "limits")
		{
			vs_interop_value* Handle = nullptr;
			Status = vs_interop_value_create(&Handle);
			if (Status != VS_INTEROP_STATUS_OK) return RaiseStatus(Self, Status, "create numeric output");
			Output.reset(Handle);
		}
		if (ValueObject)
		{
			vs_interop_value* Handle = nullptr;
			Status = vs_interop_value_create(&Handle);
			if (Status != VS_INTEROP_STATUS_OK) return RaiseStatus(Self, Status, "create numeric input");
			Input.reset(Handle);
			// Retain a snapshot so another Python thread cannot replace the input
			// Value while the native operation is running without the GIL.
			Status = vs_interop_value_assign(Input.get(), reinterpret_cast<FPyValue*>(ValueObject)->Handle);
			if (Status != VS_INTEROP_STATUS_OK) return RaiseStatus(Self, Status, "retain numeric input");
		}
		const FCallbackActivity Activity(Self ? Self->Callbacks : nullptr);
		Py_BEGIN_ALLOW_THREADS
		if (Op == "limits")
		{
			Status = vs_interop_session_set_numeric_limits(Session, &Limits);
		}
		else if (Op == "parse")
		{
			Status = vs_interop_numeric_parse(Session, Call, Kind, View(Text, TextSize), Output.get());
		}
		else if (Op == "text")
		{
			Status = vs_interop_numeric_to_text(Session, Call, Input.get(), Output.get());
		}
		else
		{
			Status = vs_interop_numeric_convert(Session, Call, Input.get(), Target, ExactFloat ? 1 : 0, Output.get());
		}
		Py_END_ALLOW_THREADS
		if (Status != VS_INTEROP_STATUS_OK) return RaiseStatus(Self, Status, "numeric operation");
		if (Op == "limits")
		{
			Py_RETURN_NONE;
		}
		if (Op == "text") return TakeValue(Output.release());
		return NewValueTakingOwnership(Output.release());
	}

	PyObject* SessionNumerics(FPySession* Self, PyObject* Args)
	{
		try
		{
			return NumericDispatch(Self, Args);
		}
		catch (const std::bad_alloc&)
		{
			return PyErr_NoMemory();
		}
		catch (...)
		{
			PyErr_SetString(GError, "numeric binding failure");
			return nullptr;
		}
	}
	PyObject* CurrentNumerics(PyObject*, PyObject* Args)
	{
		return SessionNumerics(nullptr, Args);
	}

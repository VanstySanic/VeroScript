// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

	// Included inside the binding implementation namespace after value/session helpers.

	PyTypeObject GMemoryLeaseType = {PyVarObject_HEAD_INIT(nullptr, 0)};
	struct FPyMemoryLease
	{
		PyObject_HEAD vs_interop_memory_lease Lease;
		Py_ssize_t Exports;
		bool Write;
		bool Closed;
	};
	void MemoryLeaseDealloc(FPyMemoryLease* Self)
	{
		if (!Self->Closed && Self->Lease.token.serial)
		{
			Self->Closed = true;
			(void)vs_interop_memory_release_lease(Self->Lease.token);
		}
		Py_TYPE(Self)->tp_free(reinterpret_cast<PyObject*>(Self));
	}
	PyObject* MemoryLeaseClose(FPyMemoryLease* Self, PyObject*)
	{
		if (!CheckInterpreter())
		{
			return nullptr;
		}
		if (Self->Exports)
		{
			PyErr_SetString(PyExc_BufferError, "release memoryview exports before closing the lease");
			return nullptr;
		}
		if (!Self->Closed)
		{
			// Releasing external storage may invoke a Python buffer exporter. Close
			// before entering it so reentrant calls cannot export the retiring data.
			Self->Closed = true;
			const auto Status = vs_interop_memory_release_lease(Self->Lease.token);
			if (Status != VS_INTEROP_STATUS_OK)
			{
				Self->Closed = false;
				return RaiseStatus(nullptr, Status, "release memory lease");
			}
		}
		Py_RETURN_NONE;
	}
	PyObject* MemoryLeaseEnter(FPyMemoryLease* Self, PyObject*)
	{
		if (Self->Closed)
		{
			PyErr_SetString(GError, "memory lease is closed");
			return nullptr;
		}
		Py_INCREF(Self);
		return reinterpret_cast<PyObject*>(Self);
	}
	PyObject* MemoryLeaseExit(FPyMemoryLease* Self, PyObject*)
	{
		return MemoryLeaseClose(Self, nullptr);
	}

	int MemoryLeaseGetBuffer(PyObject* Object, Py_buffer* Buffer, int Flags)
	{
		auto* Self = reinterpret_cast<FPyMemoryLease*>(Object);
		if (!CheckInterpreter())
		{
			return -1;
		}
		if (Self->Closed || Self->Lease.size > static_cast<std::uint64_t>(PY_SSIZE_T_MAX))
		{
			PyErr_SetString(PyExc_BufferError, "memory lease is closed or too large for a Python view");
			return -1;
		}
		if (PyBuffer_FillInfo(Buffer, Object, const_cast<std::uint8_t*>(Self->Lease.data),
		        static_cast<Py_ssize_t>(Self->Lease.size), !Self->Write, Flags) < 0)
		{
			return -1;
		}
		++Self->Exports;
		return 0;
	}

	void MemoryLeaseReleaseBuffer(PyObject* Object, Py_buffer*)
	{
		--reinterpret_cast<FPyMemoryLease*>(Object)->Exports;
	}
	PyObject* MemoryLeaseSize(FPyMemoryLease* Self, void*)
	{
		return PyLong_FromUnsignedLongLong(Self->Lease.size);
	}
	PyObject* MemoryLeaseClosed(FPyMemoryLease* Self, void*)
	{
		return PyBool_FromLong(Self->Closed);
	}
	PyObject* MemoryLeaseWritable(FPyMemoryLease* Self, void*)
	{
		return PyBool_FromLong(Self->Write);
	}

	PyMethodDef MemoryLeaseMethods[] = {{"close", reinterpret_cast<PyCFunction>(MemoryLeaseClose), METH_NOARGS,
	                                        "Release the lease after native use and exported views finish."},
	    {"__enter__", reinterpret_cast<PyCFunction>(MemoryLeaseEnter), METH_NOARGS, nullptr},
	    {"__exit__", reinterpret_cast<PyCFunction>(MemoryLeaseExit), METH_VARARGS, nullptr},
	    {nullptr, nullptr, 0, nullptr}};
	PyGetSetDef MemoryLeaseProperties[] = {
	    {const_cast<char*>("size"), reinterpret_cast<getter>(MemoryLeaseSize), nullptr, nullptr, nullptr},
	    {const_cast<char*>("closed"), reinterpret_cast<getter>(MemoryLeaseClosed), nullptr, nullptr, nullptr},
	    {const_cast<char*>("writable"), reinterpret_cast<getter>(MemoryLeaseWritable), nullptr, nullptr, nullptr},
	    {nullptr, nullptr, nullptr, nullptr, nullptr}};
	PyBufferProcs MemoryLeaseBuffers = {MemoryLeaseGetBuffer, MemoryLeaseReleaseBuffer};
	void InitializeMemoryLeaseType()
	{
		GMemoryLeaseType.tp_name = "veroscript.MemoryLease";
		GMemoryLeaseType.tp_basicsize = sizeof(FPyMemoryLease);
		GMemoryLeaseType.tp_flags = Py_TPFLAGS_DEFAULT;
		GMemoryLeaseType.tp_dealloc = reinterpret_cast<destructor>(MemoryLeaseDealloc);
		GMemoryLeaseType.tp_methods = MemoryLeaseMethods;
		GMemoryLeaseType.tp_getset = MemoryLeaseProperties;
		GMemoryLeaseType.tp_as_buffer = &MemoryLeaseBuffers;
	}

	struct FPyExternalMemory
	{
		Py_buffer Buffer{};
	};
	void VS_INTEROP_CALL ReleaseExternalPythonMemory(void* Owner)
	{
		const auto Gil = PyGILState_Ensure();
		auto* State = static_cast<FPyExternalMemory*>(Owner);
		PyBuffer_Release(&State->Buffer);
		delete State;
		PyGILState_Release(Gil);
	}
	PyObject* MemoryDispatch(FPySession* Self, PyObject* Args)
	{
		if (!CheckInterpreter() || (Self && !CheckSession(Self)))
		{
			return nullptr;
		}
		if (!Self && !ActiveMemoryCall)
		{
			PyErr_SetString(GError, "memory services require an active native callback");
			return nullptr;
		}
		if (PyTuple_GET_SIZE(Args) < 1 || !PyUnicode_Check(PyTuple_GET_ITEM(Args, 0)))
		{
			PyErr_SetString(PyExc_TypeError, "memory operation required");
			return nullptr;
		}
		const char* Op = PyUnicode_AsUTF8(PyTuple_GET_ITEM(Args, 0));
		if (!Op)
		{
			return nullptr;
		}
		const auto N = PyTuple_GET_SIZE(Args) - 1;
		vs_interop_session* S = Self ? Self->Handle : nullptr;
		const vs_interop_call_context* C = Self ? nullptr : ActiveMemoryCall;
		// __index__, __bool__ and Python buffer exporters can re-enter Python and
		// close the session. Never use a handle captured before those conversions.
		const auto Ready = [&]() -> bool
		{
			if (PyErr_Occurred() || (Self && !CheckSession(Self)))
			{
				return false;
			}
			S = Self ? Self->Handle : nullptr;
			C = Self ? nullptr : ActiveMemoryCall;
			if (!Self && !C)
			{
				PyErr_SetString(GError, "memory services require an active native callback");
				return false;
			}
			return true;
		};
		const auto Item = [&](Py_ssize_t I) -> PyObject*
		{
			if (I >= N)
			{
				PyErr_SetString(PyExc_TypeError, "missing memory argument");
				return nullptr;
			}
			return PyTuple_GET_ITEM(Args, I + 1);
		};
		const auto Value = [&](Py_ssize_t I) -> vs_interop_value*
		{
			auto* O = Item(I);
			if (!O)
			{
				return nullptr;
			}
			if (!PyObject_TypeCheck(O, &GValueType))
			{
				PyErr_SetString(PyExc_TypeError, "memory region/value requires Value");
				return nullptr;
			}
			auto* Handle = reinterpret_cast<FPyValue*>(O)->Handle;
			if (!Handle)
			{
				PyErr_SetString(GError, "VeroScript value is uninitialized");
			}
			return Handle;
		};
		const auto Integer = [&](Py_ssize_t I) -> std::int64_t
		{
			auto* O = Item(I);
			return O ? PyLong_AsLongLong(O) : -1;
		};
		const auto Unsigned = [&](Py_ssize_t I) -> std::uint64_t
		{
			auto* O = Item(I);
			return O ? PyLong_AsUnsignedLongLong(O) : 0;
		};
		const auto Boolean = [&](Py_ssize_t I) -> std::uint8_t
		{
			auto* O = Item(I);
			if (!O)
			{
				return 0;
			}
			const int B = PyObject_IsTrue(O);
			return B > 0 ? 1 : 0;
		};
		vs_interop_status Status = VS_INTEROP_STATUS_INVALID_ARGUMENT;
		vs_interop_value* Out = nullptr;
		std::int64_t Length = 0;
		std::uint64_t Number = 0;
		std::uint8_t Bool = 0;
		const std::string_view Operation(Op);
		const bool ReturnsValue = Operation == "buffer" || Operation == "view" || Operation == "slice" ||
		    Operation == "clone" || Operation == "from_array" || Operation == "to_array" ||
		    Operation == "read" || Operation == "external";
		if (ReturnsValue && vs_interop_value_create(&Out) != VS_INTEROP_STATUS_OK)
		{
			return PyErr_NoMemory();
		}
		struct FOutputGuard
		{
			~FOutputGuard()
			{
				if (P)
				{
					vs_interop_value_destroy(P);
				}
			}

			vs_interop_value*& P;
		} Guard{Out};
		if (Operation == "buffer")
		{
			const auto Size = Integer(0);
			if (Ready())
			{
				Status = vs_interop_memory_create_buffer(S, C, Size, Out);
			}
		}
		else if (Operation == "view")
		{
			const auto RO = Boolean(0);
			if (Ready())
			{
				Status = vs_interop_memory_create_view(S, C, RO, Out);
			}
		}
		else if (Operation == "limits")
		{
			const auto Single = Unsigned(0), All = Unsigned(1);
			vs_interop_memory_limits L{sizeof(L), 0, Single, All};
			if (Ready() && S)
			{
				Status = vs_interop_session_set_memory_limits(S, &L);
			}
		}
		else if (Operation == "stdlib_limits")
		{
			const auto Text = Unsigned(0), Read = Unsigned(1), Parts = Unsigned(2);
			vs_interop_stdlib_limits L{sizeof(L), 0, Text, Read, Parts};
			if (Ready() && S)
			{
				Status = vs_interop_session_set_stdlib_limits(S, &L);
			}
		}
		else if (Operation == "stats")
		{
			vs_interop_memory_stats T{};
			T.struct_size = sizeof(T);
			Status = vs_interop_memory_get_stats(S, C, &T);
			if (Status == VS_INTEROP_STATUS_OK)
			{
				return Py_BuildValue("(KKK)", static_cast<unsigned long long>(T.owned_bytes),
				    static_cast<unsigned long long>(T.borrowed_bytes),
				    static_cast<unsigned long long>(T.peak_owned_bytes));
			}
		}
		else if (Operation == "available")
		{
			Status = vs_interop_memory_native_addresses_available(S, C, &Bool);
		}
		else if (Operation == "argument_address")
		{
			const auto I = Unsigned(0);
			if (Ready())
			{
				Status = vs_interop_memory_call_argument_address(C, I, &Number);
			}
		}
		else if (Operation == "external")
		{
			const auto RO = Boolean(1);
			auto* O = Item(0);
			if (!Ready())
			{
				return nullptr;
			}
			auto State = std::make_unique<FPyExternalMemory>();
			if (PyObject_GetBuffer(O, &State->Buffer, RO ? PyBUF_CONTIG_RO : PyBUF_CONTIG) < 0)
			{
				return nullptr;
			}
			if (State->Buffer.len < 0)
			{
				PyBuffer_Release(&State->Buffer);
				PyErr_SetString(PyExc_ValueError, "negative buffer size");
				return nullptr;
			}
			if (!Ready())
			{
				PyBuffer_Release(&State->Buffer);
				return nullptr;
			}
			Status = vs_interop_memory_wrap_external(S, C, State->Buffer.buf,
			    static_cast<std::uint64_t>(State->Buffer.len), RO, State.get(), ReleaseExternalPythonMemory, Out);
			if (Status == VS_INTEROP_STATUS_OK)
			{
				(void)State.release();
			}
			else
			{
				PyBuffer_Release(&State->Buffer);
			}
		}
		else
		{
			auto* A = Value(0);
			if (!A)
			{
				return nullptr;
			}
			if (Operation == "size")
			{
				Status = vs_interop_memory_size(S, C, A, &Length);
			}
			else if (Operation == "capacity")
			{
				Status = vs_interop_memory_capacity(S, C, A, &Length);
			}
			else if (Operation == "id")
			{
				Status = vs_interop_memory_object_id(S, C, A, &Number);
			}
			else if (Operation == "address")
			{
				Status = vs_interop_memory_object_address(S, C, A, &Number);
			}
			else if (Operation == "data_address")
			{
				Status = vs_interop_memory_data_address(S, C, A, &Number);
			}
			else if (Operation == "version")
			{
				Status = vs_interop_memory_mutation_version(S, C, A, &Number);
			}
			else if (Operation == "invalidate")
			{
				Status = vs_interop_memory_invalidate_external(S, C, A);
			}
			else if (Operation == "clone")
			{
				Status = vs_interop_memory_clone(S, C, A, Out);
			}
			else if (Operation == "from_array")
			{
				Status = vs_interop_memory_from_array(S, C, A, Out);
			}
			else if (Operation == "to_array")
			{
				Status = vs_interop_memory_to_array(S, C, A, Out);
			}
			else if (Operation == "slice")
			{
				const auto O = Integer(1), Size = Integer(2);
				const auto RO = Boolean(3);
				if (Ready())
				{
					Status = vs_interop_memory_slice(S, C, A, O, Size, RO, Out);
				}
			}
			else if (Operation == "resize")
			{
				const auto Size = Integer(1);
				const auto Reserve = Boolean(2);
				if (Ready())
				{
					Status = vs_interop_memory_resize(S, C, A, Size, Reserve);
				}
			}
			else if (Operation == "fill")
			{
				const auto O = Integer(1), Size = Integer(2);
				const auto B = Unsigned(3);
				if (B > 255)
				{
					PyErr_SetString(PyExc_ValueError, "byte must be in [0,255]");
					return nullptr;
				}
				if (Ready())
				{
					Status = vs_interop_memory_fill(S, C, A, O, Size, static_cast<std::uint8_t>(B));
				}
			}
			else if (Operation == "copy" || Operation == "equal")
			{
				const auto AO = Integer(1);
				auto* B = Value(2);
				const auto BO = Integer(3), Size = Integer(4);
				if (Ready())
				{
					Status = Operation == "copy" ? vs_interop_memory_copy(S, C, A, AO, B, BO, Size)
					                             : vs_interop_memory_equal(S, C, A, AO, B, BO, Size, &Bool);
				}
			}
			else if (Operation == "read" || Operation == "write")
			{
				const auto O = Integer(1);
				const auto T = Unsigned(2), Order = Unsigned(3);
				vs_interop_value* B = Operation == "write" ? Value(4) : nullptr;
				if (T > std::numeric_limits<std::uint32_t>::max() || Order > 1)
				{
					PyErr_SetString(PyExc_ValueError, "invalid numeric type/byte order");
					return nullptr;
				}
				if (Ready())
				{
					Status = Operation == "read"
					    ? vs_interop_memory_read(S, C, A, O, static_cast<std::uint32_t>(T),
					          static_cast<std::uint32_t>(Order), Out)
					    : vs_interop_memory_write(S, C, A, O, static_cast<std::uint32_t>(T),
					          static_cast<std::uint32_t>(Order), B);
				}
			}
			else if (Operation == "lease")
			{
				const auto Write = Boolean(1);
				if (!Ready())
				{
					return nullptr;
				}
				auto* Lease = reinterpret_cast<FPyMemoryLease*>(GMemoryLeaseType.tp_alloc(&GMemoryLeaseType, 0));
				if (!Lease)
				{
					return nullptr;
				}
				Lease->Lease.struct_size = sizeof(Lease->Lease);
				Lease->Write = Write != 0;
				Status = vs_interop_memory_acquire_lease(S, C, A, Write, &Lease->Lease);
				if (Status != VS_INTEROP_STATUS_OK)
				{
					Lease->Closed = true;
					Py_DECREF(Lease);
					return RaiseStatus(Self, Status, "acquire memory lease");
				}
				return reinterpret_cast<PyObject*>(Lease);
			}
		}
		if (PyErr_Occurred())
		{
			return nullptr;
		}
		if (Status != VS_INTEROP_STATUS_OK)
		{
			return RaiseStatus(Self, Status, "memory operation");
		}
		if (ReturnsValue)
		{
			auto* Result = Out;
			Out = nullptr;
			return TakeValue(Result);
		}
		if (Operation == "size" || Operation == "capacity")
		{
			return PyLong_FromLongLong(Length);
		}
		if (Operation == "id" || Operation == "address" || Operation == "data_address" ||
		    Operation == "version" || Operation == "argument_address")
		{
			return PyLong_FromUnsignedLongLong(Number);
		}
		if (Operation == "equal" || Operation == "available")
		{
			return PyBool_FromLong(Bool);
		}
		Py_RETURN_NONE;
	}
	PyObject* SessionMemory(FPySession* Self, PyObject* Args)
	{
		try
		{
			return MemoryDispatch(Self, Args);
		}
		catch (const std::bad_alloc&)
		{
			return PyErr_NoMemory();
		}
		catch (...)
		{
			PyErr_SetString(GError, "memory binding failure");
			return nullptr;
		}
	}
	PyObject* CurrentMemory(PyObject*, PyObject* Args)
	{
		return SessionMemory(nullptr, Args);
	}
